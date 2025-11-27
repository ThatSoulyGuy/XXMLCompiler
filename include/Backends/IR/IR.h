#pragma once

// Convenience header that includes all IR components

#include "Backends/IR/Types.h"
#include "Backends/IR/Values.h"
#include "Backends/IR/Instructions.h"
#include "Backends/IR/BasicBlock.h"
#include "Backends/IR/Function.h"
#include "Backends/IR/Module.h"
#include "Backends/IR/IRBuilder.h"
#include "Backends/IR/Verifier.h"
#include "Backends/IR/Emitter.h"

// Namespace alias for convenience
namespace XXML {
namespace IR = Backends::IR;
}
