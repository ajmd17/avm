#ifndef OBJECT_H
#define OBJECT_H

#include <detail/reference.h>
#include <common/types.h>

#include <memory>
#include <vector>
#include <string>
#include <array>

namespace avm {
class Heap;
class Reference;
class VMState;

class Object {
public:
  enum : int { 
    FLAG_TEMPORARY = 0x01, 
    FLAG_CONST = 0x02, 
    FLAG_MARKED = 0x04 
  };

  virtual ~Object() = default;

  virtual void invoke(VMState *state, uint32_t nargs) = 0;
  virtual Reference clone(Heap &heap) = 0;

  void AddFieldReference(const AVMString_t &name, Reference ref);
  Reference GetFieldReference(const AVMString_t &name);
  Reference GetFieldReference(size_t index);

  void Mark();

  virtual std::string ToString() const = 0;
  virtual std::string TypeString() const = 0;

  int flags = 0;
  int refcount = 1;

protected:
  std::vector<std::pair<AVMString_t, Reference>> fields;

private:
  void MarkFields();
};
typedef Object* ObjectPtr;
} // namespace avm

#endif