#pragma once

#include "common.h"
#include "state.h"
#include "cacheManager.h"
#include "runtimeValue.h"
#include "interface.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/GlobalValue.h>
#include <unordered_set>
#include <queue>
using std::unordered_set;
using std::queue;
using std::tuple;

class _LabelTag {
public:
    static long inc;
    long id = ++inc;
    llvm::BasicBlock* _block;
    std::function<llvm::BasicBlock*()> lazyBlock;
    inline _LabelTag(llvm::BasicBlock* block) : _block(block) {}
    inline _LabelTag(std::function<llvm::BasicBlock*()> lazyBlock) : _block(nullptr), lazyBlock(std::move(lazyBlock)) {}
    inline llvm::BasicBlock* block() { return _block == nullptr ? _block = lazyBlock() : _block; }
};
using LabelTag = std::shared_ptr<_LabelTag>;

template<typename T>
class Local {
public:
    bool used = false;
    llvm::Value* pointer;
    Property<LlvmRuntimeValue<T>> value{
        [=]() { return LlvmRuntimeValue<T>([=]() { used = true; return Builder.CreateLoad(pointer); }); },
        [=](auto val) { used = true; Builder.CreateStore(val, pointer); }
    };
    inline Local() : pointer(Builder.CreateAlloca(LlvmType<T>())) { }
};

int Svc(ulong recompilerPtr, uint svc, ulong state);
ulong SR(ulong recompilerPtr, uint op0, uint op1, uint crn, uint crm, uint op2);
void SR(ulong recompilerPtr, uint op0, uint op1, uint crn, uint crm, uint op2, ulong value);

class EXPORTED Recompiler {
public:
    Recompiler();
    bool recompile(uint inst, ulong pc);
    void run(ulong pc, ulong sp);
    void runOne(Block* block);
    void runOne();
    void precompile(ulong pc);
    void recompileMultiple(Block* block);

    CpuState state;
    bool isOptimizer = false;
    CpuInterface* interface;
    ulong currentPC;
    void* stateLocals[sizeof(CpuState)];
    template<typename T>
    inline Local<T>* GetLocal(int i) { return (Local<T>*) stateLocals[i]; }

    llvm::Function* function;
    std::unique_ptr<llvm::Module> module;
    BlockContext context;
    bool noLocalBranches;
    bool branched;
    bool justBranched;
    long suppressedBranch;
    llvm::BasicBlock* currentBlock;
    unordered_set<long> usedLabels;
    std::vector<std::tuple<LabelTag, LabelTag>> loadRegistersLabels, storeRegistersLabels;
    unordered_map<ulong, LabelTag> blockLabels;
    queue<tuple<BlockContext, ulong>> blocksNeeded;
    LlvmRuntimeValue<ulong> CpuStateRef{nullptr};
#define FieldAddress(name) (CpuStateRef + offsetof(CpuState, name))

    Indexer<LlvmRuntimeValue<ulong>> XR{
        [=](auto reg) {
            if(reg == 31)
                return (LlvmRuntimeValue<ulong>) 0UL;
            return GetLocal<ulong>(offsetof(CpuState, X0) + reg * 8)->value();
        },
        [=](auto reg, auto value) {
            if(reg == 31) {
                value.Emit();
                return;
            }
            GetLocal<ulong>(offsetof(CpuState, X0) + reg * 8)->value = value;
        }
    };
    Indexer<LlvmRuntimeValue<Vector128<float>>> VR{
        [=](auto reg) {
            return GetLocal<Vector128<float>>(offsetof(CpuState, V0) + (reg * 16))->value;
        },
        [=](auto reg, auto value) {
            GetLocal<Vector128<float>>(offsetof(CpuState, V0) + (reg * 16))->value = value;
        }
    };
    Indexer<LlvmRuntimeValue<uint8_t>> VBR{
        [=](auto reg) {
            auto vec = GetLocal<Vector128<float>>(offsetof(CpuState, V0) + (reg * 16))->value();
            return vec.template Element<uint8_t>(0);
        },
        [=](auto reg, auto value) {
            GetLocal<Vector128<float>>(offsetof(CpuState, V0) + (reg * 16))->value = LlvmRuntimeValue<Vector128<uint8_t>>([value]() {
                auto bvec = Builder.CreateInsertElement(llvm::UndefValue::get(LlvmType<Vector128<uint8_t>>()), value, (LlvmRuntimeValue<int>) 0);
                for(auto i = 1; i < 16; ++i)
                    bvec = Builder.CreateInsertElement(bvec, (LlvmRuntimeValue<uint8_t>) 0, (LlvmRuntimeValue<int>) i);
                return bvec;
            });
        }
    };
    Indexer<LlvmRuntimeValue<ushort>> VHR{
            [=](auto reg) {
                auto vec = GetLocal<Vector128<float>>(offsetof(CpuState, V0) + (reg * 16))->value();
                return vec.template Element<ushort>(0);
            },
            [=](auto reg, auto value) {
                GetLocal<Vector128<float>>(offsetof(CpuState, V0) + (reg * 16))->value = LlvmRuntimeValue<Vector128<ushort>>([value]() {
                    auto bvec = Builder.CreateInsertElement(llvm::UndefValue::get(LlvmType<Vector128<ushort>>()), value, (LlvmRuntimeValue<int>) 0);
                    for(auto i = 1; i < 8; ++i)
                        bvec = Builder.CreateInsertElement(bvec, (LlvmRuntimeValue<ushort>) 0, (LlvmRuntimeValue<int>) i);
                    return bvec;
                });
            }
    };
    Indexer<LlvmRuntimeValue<float>> VSR{
            [=](auto reg) {
                auto vec = GetLocal<Vector128<float>>(offsetof(CpuState, V0) + (reg * 16))->value();
                return vec.template Element<float>(0);
            },
            [=](auto reg, auto value) {
                GetLocal<Vector128<float>>(offsetof(CpuState, V0) + (reg * 16))->value = LlvmRuntimeValue<Vector128<float>>([value]() {
                    auto bvec = Builder.CreateInsertElement(llvm::UndefValue::get(LlvmType<Vector128<float>>()), value, (LlvmRuntimeValue<int>) 0);
                    for(auto i = 1; i < 4; ++i)
                        bvec = Builder.CreateInsertElement(bvec, (LlvmRuntimeValue<float>) 0, (LlvmRuntimeValue<int>) i);
                    return bvec;
                });
            }
    };
    Indexer<LlvmRuntimeValue<double>> VDR{
            [=](auto reg) {
                auto vec = GetLocal<Vector128<float>>(offsetof(CpuState, V0) + (reg * 16))->value();
                return vec.template Element<double>(0);
            },
            [=](auto reg, auto value) {
                GetLocal<Vector128<float>>(offsetof(CpuState, V0) + (reg * 16))->value = LlvmRuntimeValue<Vector128<double>>([value]() {
                    auto bvec = Builder.CreateInsertElement(llvm::UndefValue::get(LlvmType<Vector128<double>>()), value, (LlvmRuntimeValue<int>) 0);
                    return Builder.CreateInsertElement(bvec, (LlvmRuntimeValue<double>) 0.0, (LlvmRuntimeValue<int>) 1);
                });
            }
    };

#define DIRECT_FIELD(name) Property<LlvmRuntimeValue<decltype(CpuState::name)>> name##R{ \
    [=]() { return Field<decltype(CpuState::name)>(offsetof(CpuState, name)); }, \
    [=](auto value) { Field(offsetof(CpuState, name), value); } \
}

#define FIELD(name) Property<LlvmRuntimeValue<decltype(CpuState::name)>> name##R{ \
    [=]() { return GetLocal<decltype(CpuState::name)>(offsetof(CpuState, name))->value; }, \
    [=](auto value) { GetLocal<decltype(CpuState::name)>(offsetof(CpuState, name))->value = value; } \
}

    DIRECT_FIELD(BranchTo);
    FIELD(PC);
    FIELD(SP);
    FIELD(Exclusive8);
    FIELD(Exclusive16);
    FIELD(Exclusive32);
    FIELD(Exclusive64);
    FIELD(NZCV_N);
    FIELD(NZCV_Z);
    FIELD(NZCV_C);
    FIELD(NZCV_V);
    Property<LlvmRuntimeValue<ulong>> NZCVR{
        [=]() { return (NZCV_NR() << 31) | (NZCV_ZR() << 30) | (NZCV_CR() << 29) | (NZCV_VR() << 28); },
        [=](auto value) {
            NZCV_NR = (value >> 31) & 1;
            NZCV_ZR = (value >> 30) & 1;
            NZCV_CR = (value >> 29) & 1;
            NZCV_VR = (value >> 28) & 1;
        }
    };

    template<typename T>
    inline LlvmRuntimeValue<T> Field(int offset) const {
        return LlvmRuntimeValue<T>([=]() {
            auto addr = CpuStateRef + offset;
            auto ptr = Builder.CreateIntToPtr(addr.Emit(), LlvmType<T*>());
            return Builder.CreateLoad(ptr);
        });
    }

    template<typename T>
    inline void Field(int offset, LlvmRuntimeValue<T> value) const {
        auto addr = CpuStateRef + offset;
        auto ptr = Builder.CreateIntToPtr(addr.Emit(), LlvmType<T*>());
        Builder.CreateStore(value, ptr);
    }

    template<typename T>
    inline LlvmRuntimeValue<T> SignExtRuntime(LlvmRuntimeValue<ulong> value, int size) const {
        return LlvmRuntimeValue<T>([=]() {
            return Builder.CreateSExt(Builder.CreateTrunc(value, llvm::Type::getIntNTy(Builder.getContext(), size)), LlvmType<T>());
        });
    }

    template<typename RetType, typename ... ArgTypes, typename = std::enable_if_t<!std::is_void_v<RetType>>>
    inline LlvmRuntimeValue<RetType> Call(RetType (*func)(ArgTypes ...), LlvmRuntimeValue<ArgTypes> ... args) const {
        return LlvmRuntimeValue<RetType>([=]() {
            return Builder.CreateCall(Builder.CreateIntToPtr((LlvmRuntimeValue<ulong>) (ulong) func, LlvmType<std::function<RetType(ArgTypes ...)>*>()), {args.Emit()... });
        });
    }

    template<typename RetType, typename ... ArgTypes, typename = std::enable_if_t<std::is_void_v<RetType>>>
    inline void Call(void (*func)(ArgTypes ...), LlvmRuntimeValue<ArgTypes> ... args) const {
        Builder.CreateCall(Builder.CreateIntToPtr((LlvmRuntimeValue<ulong>) (ulong) func, LlvmType<std::function<void(ArgTypes ...)>>()), {args.Emit()... });
    }

    inline LabelTag DefineLabel() const { return std::make_shared<_LabelTag>([=]() { return llvm::BasicBlock::Create(Builder.getContext(), "", function); }); }
    inline void Label(LabelTag label) {
        if(justBranched && suppressedBranch == label->id && !usedLabels.count(label->id)) {
            justBranched = false;
            return;
        }
        justBranched = false;
        Builder.SetInsertPoint(currentBlock = label->block());
    }

    inline void CallSvc(uint svc) {
        auto preStore = DefineLabel(), postStore = DefineLabel();
        auto preLoad = DefineLabel(), postLoad = DefineLabel();
        storeRegistersLabels.push_back({ preStore, postStore });
        loadRegistersLabels.push_back({ preLoad, postLoad });
        Branch(preStore);
        Label(postStore);
        auto cont = Call<int, ulong, uint, ulong>(Svc, (ulong) this, svc, CpuStateRef);
        BranchIf(cont == 1, preLoad, std::get<1>(storeRegistersLabels[0]));
        Label(postLoad);
    }

    inline void Branch(LabelTag label) {
        if(!justBranched) {
            Builder.CreateBr(label->block());
            usedLabels.insert(label->id);
        } else
            suppressedBranch = label->id;
        justBranched = true;
    }
    inline void BranchIf(const LlvmRuntimeValue<bool>& cond, LabelTag _if, LabelTag _else) {
        if(justBranched) return;
        usedLabels.insert(_if->id);
        usedLabels.insert(_else->id);
        Builder.CreateCondBr(cond, _if->block(), _else->block());
        justBranched = true;
    }

    inline void WithLink(const std::function<void()>& func) {
        auto next = currentPC + 4;
        XR[30] = next;
        // Handle case where the next instruction after branch is bad
        // Or we're only recompiling a single block for debugging
        if(noLocalBranches || getInstructionClass(*(uint*) next) == nullptr) {
            func();
            return;
        }
        if(!blockLabels.count(next)) {
            blockLabels[next] = DefineLabel();
            blocksNeeded.push({context, next});
        }

        auto old = context;
        context = old;
        context.LR = next;
        func();
        context = old;
    }

    inline void Branch(ulong addr) {
        if(noLocalBranches) {
            BranchToR = addr;
            Branch(std::get<0>(storeRegistersLabels[0]));
            branched = true;
            return;
        }
        LabelTag label;
        if(blockLabels.count(addr) == 0) {
            label = blockLabels[addr] = DefineLabel();
            blocksNeeded.push({context, addr});
        } else
            label = blockLabels[addr];
        Branch(label);
        branched = true;
    }
    inline void BranchLinked(ulong addr) { WithLink([=]() { Branch(addr); }); }

    inline void BranchRegister(int reg) {
        auto base = [&]() {
            BranchToR = XR[reg]();
            Branch(std::get<0>(storeRegistersLabels[0]));
            branched = true;
        };
        // Uncomment to follow paths after bl(r) instructions
        //if(reg != 30 || context.LR == -1UL) {
            base();
            return;
        //}

        auto if_ = DefineLabel(), else_ = DefineLabel();
        BranchIf(XR[30]() == context.LR, if_, else_);
        Label(if_);
        Branch(context.LR);
        Label(else_);
        base();
    }
    inline void BranchLinkedRegister(int reg) { WithLink([=]() { BranchRegister(reg); }); }

    template<typename T>
    inline LlvmRuntimeValue<uint8_t> CompareAndSwap(LlvmRuntimePointer<T> pointer, LlvmRuntimeValue<T> value, LlvmRuntimeValue<T> comparand) {
        return LlvmRuntimeValue<uint8_t>([=]() {
            return Builder.CreateSelect(
                    Builder.CreateExtractValue(
                    Builder.CreateAtomicCmpXchg(
                        pointer.pointer, comparand, value,
                        llvm::AtomicOrdering::SequentiallyConsistent,
                        llvm::AtomicOrdering::SequentiallyConsistent,
                        false
                    ), 1
                ),
                    (LlvmRuntimeValue<uint8_t>) 0, (LlvmRuntimeValue<uint8_t>) 1
            );
        });
    }

#include "recompiler.generated.h"
};

template<typename CondT, typename ValueT>
inline LlvmRuntimeValue<ValueT> Ternary(LlvmRuntimeValue<CondT> cond, LlvmRuntimeValue<ValueT> a, LlvmRuntimeValue<ValueT> b) {
    return LlvmRuntimeValue<ValueT>([=]() {
        auto rec = &RecompilerInstance;
        auto if_ = rec->DefineLabel(), else_ = rec->DefineLabel(), end = rec->DefineLabel();
        rec->BranchIf(cond, if_, else_);
        rec->Label(if_);
        auto av = a.Emit();
        auto ab = rec->currentBlock;
        rec->Branch(end);
        rec->Label(else_);
        auto bv = b.Emit();
        auto bb = rec->currentBlock;
        rec->Branch(end);
        rec->Label(end);
        auto phi = Builder.CreatePHI(LlvmType<ValueT>(), 2);
        phi->addIncoming(av, ab);
        phi->addIncoming(bv, bb);
        return phi;
    });
}
