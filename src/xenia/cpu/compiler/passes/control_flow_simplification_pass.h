/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_COMPILER_PASSES_CONTROL_FLOW_SIMPLIFICATION_PASS_H_
#define XENIA_COMPILER_PASSES_CONTROL_FLOW_SIMPLIFICATION_PASS_H_

#include "xenia/cpu/compiler/compiler_pass.h"

namespace xe {
namespace cpu {
namespace compiler {
namespace passes {

class ControlFlowSimplificationPass : public CompilerPass {
 public:
  ControlFlowSimplificationPass();
  ~ControlFlowSimplificationPass() override;

  bool Run(hir::HIRBuilder* builder) override;

 private:
};

}  // namespace passes
}  // namespace compiler
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_COMPILER_PASSES_CONTROL_FLOW_SIMPLIFICATION_PASS_H_
