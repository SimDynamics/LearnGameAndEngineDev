// Select only the arithmetic rule used by this opcode. Experimental profiles
// are immutable dispatch metadata; profile0 remains the ordinary SMO program.
#if defined(SMO_VM_OPCODE) && SMO_VM_OPCODE >= 5 && SMO_VM_OPCODE <= 10
#define D12_ONLY_OPERATION (SMO_VM_OPCODE - 5)
#endif
