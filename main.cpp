#include "llvm/ADT/STLExtras.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/IR/Verifier.h"

#include <nlohmann/json.hpp>

#include <cstddef> // for offsetof
#include <iostream>
#include <fstream>
#include <system_error>
#include <vector>
#include <cstdlib>

using namespace llvm;
using namespace llvm::orc;

struct MyStruct {
    int field1;   // 对应 i32
    float field2; // 对应 float
};

ExitOnError ExitOnErr;

LLVMContext TheContext;
IRBuilder<> Builder(TheContext);
std::unique_ptr<Module> TheModule;
std::unique_ptr<LLJIT> TheJIT;

std::map<std::string, llvm::Type *> typeMap = {
    {"i32", Builder.getInt32Ty()},
    {"float", Builder.getFloatTy()},
    {"double", Builder.getDoubleTy()}
};

Function *createFunction(Module &M, const std::string &name, StructType *SType) {
    // 注意：这里假设你想返回一个指向结构体的指针
    Type *returnType = PointerType::get(SType, 0);
    FunctionType *FT = FunctionType::get(returnType, false);
    Function *F = Function::Create(FT, Function::ExternalLinkage, name, &M);
    return F;
}

Function *initializeStruct(Module &M, StructType *SType, Function *F) {
    BasicBlock *BB = BasicBlock::Create(TheContext, "entry", F);
    Builder.SetInsertPoint(BB);

    AllocaInst *StructAlloc = Builder.CreateAlloca(SType, nullptr, "myStruct");

    for (unsigned i = 0, e = SType->getNumElements(); i != e; ++i) {
        auto *ElementType = SType->getElementType(i);
        Value *ElementValue = nullptr;
        if (ElementType->isIntegerTy()) {
            outs() << "isIntegerTy: " << "\n";
            ElementValue = ConstantInt::get(ElementType, 42);
        } else if (ElementType->isFloatingPointTy()) {
            outs() << "isFloatingPointTy: " << "\n";
            ElementValue = ConstantFP::get(ElementType, 3.14);
        } else {
            outs() << "Unsupported element type: " << "\n";
            assert(false && "Unsupported element type");
        }

        std::vector<Value *> indices = {
            ConstantInt::get(Type::getInt32Ty(TheContext), 0),
            ConstantInt::get(Type::getInt32Ty(TheContext), i)
        };
        Value *ElementPtr = Builder.CreateInBoundsGEP(SType, StructAlloc, indices);
        Builder.CreateStore(ElementValue, ElementPtr);
        std::cout << "Initialized: " << i << "\n";
    }

    //Builder.CreateRetVoid();
    // 修改这里来返回结构体的指针而非 void
    Builder.CreateRet(StructAlloc);

    verifyFunction(*F);
    return F;
}

int main(int argc, char *argv[]) {
    InitLLVM X(argc, argv);

    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    InitializeNativeTargetAsmParser();

    TheModule = std::make_unique<Module>("MyModule", TheContext);

    auto JTMB = ExitOnErr(JITTargetMachineBuilder::detectHost());
    auto DL = ExitOnErr(JTMB.getDefaultDataLayoutForTarget());
    TheModule->setDataLayout(DL);

    // 读取和解析配置文件
    std::ifstream configStream("structs.json");
    if (!configStream.is_open()) {
        errs() << "Failed to open structs.json\n";
        return 1;
    }
    nlohmann::json structsJson;
    configStream >> structsJson;

    std::vector<std::string> functionNames;
    for (const auto &structDef : structsJson) {
        std::string structName = structDef["name"].get<std::string>();
        std::vector<Type *> fieldTypes;
        for (const auto &fieldDef : structDef["fields"]) {
            std::string fieldType = fieldDef["type"].get<std::string>();
            fieldTypes.push_back(typeMap[fieldType]);
        }

        StructType *structType = StructType::create(TheContext, fieldTypes, structName);

        // 创建一个函数用于初始化结构体
        std::string funcName = "init_" + structName;
        Function *F = createFunction(*TheModule, funcName, structType);
        initializeStruct(*TheModule, structType, F);

        functionNames.push_back(funcName);
    }

    // 创建JIT
    TheJIT = ExitOnErr(LLJITBuilder().create());
    // 添加模块到JIT
    ExitOnErr(TheJIT->addIRModule(ThreadSafeModule(std::move(TheModule), std::make_unique<LLVMContext>())));

    // 执行初始化函数
    for (const auto &funcName : functionNames) {
        auto Sym = ExitOnErr(TheJIT->lookup(funcName));
        void *(*func)() = (void *(*)())Sym.getAddress();
        void *mptr = func(); // 调用初始化函数

        char* basePtr = reinterpret_cast<char*>(mptr);
        // 假设intMember位于结构体的开始位置，floatMember紧随其后
        int* intMemberPtr = reinterpret_cast<int*>(basePtr);
        float* floatMemberPtr = reinterpret_cast<float*>(basePtr + sizeof(int));

        std::cout << "Offset of field1 in MyStruct: " << offsetof(MyStruct, field1) << std::endl;
        std::cout << "Offset of field2 in MyStruct: " << offsetof(MyStruct, field2) << std::endl;
        std::cout << "intMember is: " << *intMemberPtr << std::endl;
        std::cout << "floatMember is: " << *floatMemberPtr << std::endl;

        outs() << "Executed: " << funcName << "\n";
        outs() << "ptr: " << mptr << "\n";
        outs() << "intptr: " << intMemberPtr << "\n";
        outs() << "floatptr: " << floatMemberPtr << "\n";
    }

    return 0;
}