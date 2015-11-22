/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/trace_player.h"

#include "xenia/gpu/command_processor.h"
#include "xenia/gpu/graphics_system.h"
#include "xenia/memory.h"

namespace xe {
namespace gpu {

TracePlayer::TracePlayer(xe::ui::Loop* loop, GraphicsSystem* graphics_system)
    : loop_(loop),
      graphics_system_(graphics_system),
      current_frame_index_(0),
      current_command_index_(-1) {
  // Need to allocate all of physical memory so that we can write to it
  // during playback.
  graphics_system_->memory()
      ->LookupHeapByType(true, 4096)
      ->AllocFixed(0, 0x1FFFFFFF, 4096,
                   kMemoryAllocationReserve | kMemoryAllocationCommit,
                   kMemoryProtectRead | kMemoryProtectWrite);
}

TracePlayer::~TracePlayer() = default;

const TraceReader::Frame* TracePlayer::current_frame() const {
  if (current_frame_index_ > frame_count()) {
    return nullptr;
  }
  return frame(current_frame_index_);
}

void TracePlayer::SeekFrame(int target_frame) {
  if (current_frame_index_ == target_frame) {
    return;
  }
  current_frame_index_ = target_frame;
  auto frame = current_frame();
  current_command_index_ = int(frame->commands.size()) - 1;

  assert_true(frame->start_ptr <= frame->end_ptr);
  PlayTrace(frame->start_ptr, frame->end_ptr - frame->start_ptr,
            TracePlaybackMode::kBreakOnSwap);
}

void TracePlayer::SeekCommand(int target_command) {
  if (current_command_index_ == target_command) {
    return;
  }
  int previous_command_index = current_command_index_;
  current_command_index_ = target_command;
  if (current_command_index_ == -1) {
    return;
  }
  auto frame = current_frame();
  const auto& command = frame->commands[target_command];
  assert_true(frame->start_ptr <= command.end_ptr);
  if (target_command && previous_command_index == target_command - 1) {
    // Seek forward.
    const auto& previous_command = frame->commands[target_command - 1];
    PlayTrace(previous_command.end_ptr,
              command.end_ptr - previous_command.end_ptr,
              TracePlaybackMode::kBreakOnSwap);
  } else {
    // Full playback from frame start.
    PlayTrace(frame->start_ptr, command.end_ptr - frame->start_ptr,
              TracePlaybackMode::kBreakOnSwap);
  }
}

void TracePlayer::PlayTrace(const uint8_t* trace_data, size_t trace_size,
                            TracePlaybackMode playback_mode) {
  graphics_system_->command_processor()->CallInThread(
      [this, trace_data, trace_size, playback_mode]() {
        PlayTraceOnThread(trace_data, trace_size, playback_mode);
      });
}

void TracePlayer::PlayTraceOnThread(const uint8_t* trace_data,
                                    size_t trace_size,
                                    TracePlaybackMode playback_mode) {
  auto memory = graphics_system_->memory();
  auto command_processor = graphics_system_->command_processor();

  command_processor->set_swap_mode(SwapMode::kIgnored);
  player_start_ptr_ = trace_data;
  player_target_ptr_ = trace_data + trace_size;
  player_current_ptr_ = trace_data;

  playing_trace_ = true;
  auto trace_ptr = trace_data;
  bool pending_break = false;
  const PacketStartCommand* pending_packet = nullptr;
  while (trace_ptr < trace_data + trace_size) {
    player_current_ptr_ = trace_ptr;

    auto type = static_cast<TraceCommandType>(xe::load<uint32_t>(trace_ptr));
    switch (type) {
      case TraceCommandType::kPrimaryBufferStart: {
        auto cmd =
            reinterpret_cast<const PrimaryBufferStartCommand*>(trace_ptr);
        //
        trace_ptr += sizeof(*cmd) + cmd->count * 4;
        break;
      }
      case TraceCommandType::kPrimaryBufferEnd: {
        auto cmd = reinterpret_cast<const PrimaryBufferEndCommand*>(trace_ptr);
        //
        trace_ptr += sizeof(*cmd);
        break;
      }
      case TraceCommandType::kIndirectBufferStart: {
        auto cmd =
            reinterpret_cast<const IndirectBufferStartCommand*>(trace_ptr);
        //
        trace_ptr += sizeof(*cmd) + cmd->count * 4;
        break;
      }
      case TraceCommandType::kIndirectBufferEnd: {
        auto cmd = reinterpret_cast<const IndirectBufferEndCommand*>(trace_ptr);
        //
        trace_ptr += sizeof(*cmd);
        break;
      }
      case TraceCommandType::kPacketStart: {
        auto cmd = reinterpret_cast<const PacketStartCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd);
        std::memcpy(memory->TranslatePhysical(cmd->base_ptr), trace_ptr,
                    cmd->count * 4);
        trace_ptr += cmd->count * 4;
        pending_packet = cmd;
        break;
      }
      case TraceCommandType::kPacketEnd: {
        auto cmd = reinterpret_cast<const PacketEndCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd);
        if (pending_packet) {
          command_processor->ExecutePacket(pending_packet->base_ptr,
                                           pending_packet->count);
          pending_packet = nullptr;
        }
        if (pending_break) {
          return;
        }
        break;
      }
      case TraceCommandType::kMemoryRead: {
        auto cmd = reinterpret_cast<const MemoryReadCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd);
        if (cmd->full_length) {
          DecompressMemory(trace_ptr, cmd->length,
                           memory->TranslatePhysical(cmd->base_ptr),
                           cmd->full_length);
        } else {
          std::memcpy(memory->TranslatePhysical(cmd->base_ptr), trace_ptr,
                      cmd->length);
        }
        trace_ptr += cmd->length;
        break;
      }
      case TraceCommandType::kMemoryWrite: {
        auto cmd = reinterpret_cast<const MemoryWriteCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd);
        // ?
        trace_ptr += cmd->length;
        break;
      }
      case TraceCommandType::kEvent: {
        auto cmd = reinterpret_cast<const EventCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd);
        switch (cmd->event_type) {
          case EventType::kSwap: {
            if (playback_mode == TracePlaybackMode::kBreakOnSwap) {
              pending_break = true;
            }
            break;
          }
        }
        break;
      }
    }
  }

  playing_trace_ = false;
  command_processor->set_swap_mode(SwapMode::kNormal);
  command_processor->IssueSwap(0, 1280, 720);
}

}  // namespace gpu
}  // namespace xe
