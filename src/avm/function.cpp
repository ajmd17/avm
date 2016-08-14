#include <detail/function.h>

#include <avm.h>
#include <detail/exception.h>
#include <detail/vm_state.h>
#include <common/util/logger.h>

namespace avm {
Func::Func(uint64_t addr, uint32_t nargs, bool is_variadic) 
  : addr(addr), nargs(nargs), is_variadic(is_variadic) {
}

void Func::invoke(VMState *state, uint32_t callargs) {
  if (callargs != nargs) {
    for (uint32_t i = 0; i < callargs; i++) {
      state->vm->PopStack();
    }
    state->HandleException(InvalidArgsException(nargs, callargs));
  } else {
    state->jump_positions.push(state->stream->Position());
    ++state->read_level;

    state->stream->Seek(addr);

    // read instructions till function is completed
    while (state->stream->Position() < state->stream->Max()) {
      Opcode_t ins;
      state->stream->Read(&ins);
      state->vm->HandleInstruction(ins);

      if (ins == ins_return) {

        state->stream->Seek(state->jump_positions.top());
        state->jump_positions.pop();
        debug_log("Popping back to position: %d", state->stream->Position());

        break;
      }
    }
  }
}

uint64_t Func::Address() const {
  return addr;
}

size_t Func::NumArgs() const {
  return nargs;
}

Reference Func::clone(Heap &heap) {
  auto ref = Reference(*heap.AllocObject<Func>(addr, nargs, is_variadic));

  // copy all members
  for (auto &&member : fields) {
    if (member.second.Ref() != nullptr) {
      ref.Ref()->AddFieldReference(member.first, member.second.Ref()->clone(heap));
    }
  }

  return ref;
}

std::string Func::ToString() const {
  return "<" + TypeString() + ">";
}

std::string Func::TypeString() const {
  return "func";
}
} // namespace avm