#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>

#include "argparse.h"
#include "libparen.h"
#include "llvm-c/Analysis.h"
#include "llvm-c/Core.h"
#include "llvm-c/Target.h"
#include "llvm-c/TargetMachine.h"
#include "llvm-c/Transforms/PassBuilder.h"

namespace {

void HandleLLVMError(char *error) {
  if (error && strlen(error) != 0)
    std::cerr << "LLVM Error: " << error << std::endl;
  LLVMDisposeMessage(error);
}

// We want to use the LLVM C API to make it easier to port to the language
// (since we won't have C++ mangling), but RAII is useful for doing cleanup from
// the C API. This is a wrapper for just that.
template <typename T>
struct GenericRAII {
  using Deleter = void (*)(T);

  GenericRAII(T ref, Deleter deleter) : ref(ref), deleter(deleter) {}
  GenericRAII(T ref) : ref(ref), deleter(nullptr) {}

  ~GenericRAII() {
    if (deleter)
      deleter(ref);
  }

  T &operator*() { return ref; }
  operator bool() const { return bool(ref); }

  T ref;
  Deleter deleter;
};

LLVMValueRef GetParenInitFunc(LLVMModuleRef mod) {
  if (LLVMValueRef func = LLVMGetNamedFunction(mod, kParenInitName.data()))
    return func;

  LLVMTypeRef param_types[] = {};
  LLVMTypeRef func_type = LLVMFunctionType(LLVMVoidType(), param_types,
                                           /*ParamCount=*/0, /*IsVarArg=*/0);
  LLVMValueRef paren_init_func =
      LLVMAddFunction(mod, kParenInitName.data(), func_type);
  return paren_init_func;
}

LLVMTypeRef GetOpaquePtr(LLVMModuleRef mod, unsigned address_space = 0) {
  return LLVMPointerTypeInContext(LLVMGetModuleContext(mod), address_space);
}

LLVMValueRef GetParenEvalStringFunc(LLVMModuleRef mod) {
  if (LLVMValueRef func =
          LLVMGetNamedFunction(mod, kParenEvalStringName.data()))
    return func;

  LLVMTypeRef param_types[] = {GetOpaquePtr(mod)};
  LLVMTypeRef func_type = LLVMFunctionType(LLVMVoidType(), param_types,
                                           /*ParamCount=*/1, /*IsVarArg=*/0);
  LLVMValueRef paren_eval_string_func =
      LLVMAddFunction(mod, kParenEvalStringName.data(), func_type);
  return paren_eval_string_func;
}

LLVMValueRef GetParenImportFunc(LLVMModuleRef mod) {
  if (LLVMValueRef func = LLVMGetNamedFunction(mod, kParenImportName.data()))
    return func;

  LLVMTypeRef param_types[] = {GetOpaquePtr(mod)};
  LLVMTypeRef func_type = LLVMFunctionType(LLVMVoidType(), param_types,
                                           /*ParamCount=*/1, /*IsVarArg=*/0);
  return LLVMAddFunction(mod, kParenImportName.data(), func_type);
}

LLVMValueRef CreateMain(LLVMModuleRef mod, std::string_view contents,
                        std::span<const std::string> imports) {
  LLVMTypeRef param_types[] = {};
  LLVMTypeRef func_type = LLVMFunctionType(LLVMInt32Type(), param_types,
                                           /*ParamCount=*/0, /*IsVarArg=*/0);
  LLVMValueRef main_func = LLVMAddFunction(mod, "main", func_type);

  LLVMBasicBlockRef entry = LLVMAppendBasicBlock(main_func, "entry");

  GenericRAII<LLVMBuilderRef> builder(LLVMCreateBuilder(), LLVMDisposeBuilder);
  LLVMPositionBuilderAtEnd(*builder, entry);

  // Initialize the runtime.
  LLVMValueRef paren_init_func = GetParenInitFunc(mod);
  LLVMValueRef paren_init_params[] = {};
  LLVMBuildCall2(*builder, /*FuncType=*/LLVMGlobalGetValueType(paren_init_func),
                 paren_init_func, paren_init_params, /*NumArgs=*/0,
                 /*Name=*/"");

  // Call any imports.
  for (std::string_view import_module : imports) {
    namespace fs = std::filesystem;
    fs::path path(import_module);

    LLVMValueRef paren_import_func = GetParenImportFunc(mod);
    LLVMValueRef path_ptr = LLVMBuildGlobalStringPtr(
        *builder, fs::absolute(path).c_str(), /*Name=*/"");
    LLVMValueRef paren_import_params[] = {path_ptr};
    LLVMBuildCall2(*builder,
                   /*FuncType=*/LLVMGlobalGetValueType(paren_import_func),
                   paren_import_func, paren_import_params,
                   /*NumArgs=*/1, /*Name=*/"");
  }

  // Just run the code.
  LLVMValueRef paren_eval_string_func = GetParenEvalStringFunc(mod);
  LLVMValueRef code =
      LLVMBuildGlobalStringPtr(*builder, contents.data(), /*Name=*/"");
  LLVMValueRef paren_eval_string_params[] = {code};
  LLVMBuildCall2(*builder,
                 /*FuncType=*/LLVMGlobalGetValueType(paren_eval_string_func),
                 paren_eval_string_func, paren_eval_string_params,
                 /*NumArgs=*/1, /*Name=*/"");

  LLVMValueRef zero = LLVMConstInt(LLVMInt32Type(), 0, /*SignExtend*/ 0);
  LLVMBuildRet(*builder, zero);

  return main_func;
}

enum class EmissionKind {
  IR,
  ASM,
  Object,
};

int Compile(std::string_view input_filename, std::ostream &out,
            EmissionKind emission, std::span<const std::string> imports = {}) {
  std::ifstream input(input_filename.data());

  LLVMInitializeX86TargetInfo();
  LLVMInitializeX86Target();
  LLVMInitializeX86TargetMC();
  LLVMInitializeX86AsmParser();
  LLVMInitializeX86AsmPrinter();

  GenericRAII<char *> triple(LLVMGetDefaultTargetTriple(), LLVMDisposeMessage);
  GenericRAII<char *> error(NULL, HandleLLVMError);
  LLVMTargetRef target;
  LLVMBool failed = LLVMGetTargetFromTriple(*triple, &target, &*error);
  if (failed)
    return -1;

  GenericRAII<char *> cpu(LLVMGetHostCPUName(), LLVMDisposeMessage);
  GenericRAII<char *> features(LLVMGetHostCPUFeatures(), LLVMDisposeMessage);
  GenericRAII<LLVMTargetMachineRef> target_machine(
      LLVMCreateTargetMachine(
          target, *triple, *cpu, *features, /*level=*/LLVMCodeGenLevelNone,
          /*Reloc=*/LLVMRelocPIC, /*CodeModel=*/LLVMCodeModelDefault),
      LLVMDisposeTargetMachine);

  LLVMModuleRef mod = LLVMModuleCreateWithName(input_filename.data());

  std::string contents;
  if (!libparen::slurp(input_filename, contents)) {
    std::cerr << "Failed to read " << input_filename << std::endl;
    return -1;
  }
  CreateMain(mod, contents, imports);

  GenericRAII<LLVMPassBuilderOptionsRef> pb_options(
      LLVMCreatePassBuilderOptions(), LLVMDisposePassBuilderOptions);
  // NOTE: This means we always need to pass `-fsanitize=address` during the
  // link step.
  LLVMErrorRef maybe_error =
      LLVMRunPasses(mod, /*passes=*/"asan", *target_machine, *pb_options);
  if (maybe_error) {
    char *error = LLVMGetErrorMessage(maybe_error);
    std::cerr << "LLVM error: " << error << std::endl;
    LLVMDisposeErrorMessage(error);
    return -1;
  }

  failed = LLVMVerifyModule(mod, LLVMAbortProcessAction, &*error);

  if (failed)
    return -1;

  GenericRAII<LLVMMemoryBufferRef> outbuff(nullptr, [](auto x) {
    if (x)
      LLVMDisposeMemoryBuffer(x);
  });

  switch (emission) {
    case EmissionKind::IR: {
      char *str = LLVMPrintModuleToString(mod);
      out << str;
      LLVMDisposeMessage(str);
      break;
    }
    case EmissionKind::ASM:
      failed = LLVMTargetMachineEmitToMemoryBuffer(*target_machine, mod,
                                                   /*codegen=*/LLVMAssemblyFile,
                                                   &*error, &*outbuff);
      break;
    case EmissionKind::Object:
      failed = LLVMTargetMachineEmitToMemoryBuffer(*target_machine, mod,
                                                   /*codegen=*/LLVMObjectFile,
                                                   &*error, &*outbuff);
      break;
  }

  if (outbuff) {
    out.write(LLVMGetBufferStart(*outbuff),
              static_cast<std::streamsize>(LLVMGetBufferSize(*outbuff)));
  }

  out.flush();

  return failed ? -1 : 0;
}

}  // namespace

int main(int argc, char *argv[]) {
  argparse::ArgParser argparser(argv[0]);
  argparser.AddPosArg("input");
  argparser.AddOptArg("compile", 'c').setStoreTrue();
  argparser.AddOptArg("output", 'o');
  argparser.AddOptArg("import", 'i').setAppend().setDefaultList();

  // TODO: This could be a mutually exclusive group.
  argparser.AddOptArg("emit-llvm").setStoreTrue();
  argparser.AddOptArg("emit-asm").setStoreTrue();

  auto args = argparser.ParseArgs(argc, argv);
  if (args.HelpIsSet()) {
    argparser.PrintHelp();
    return 0;
  }

  if (!args.has("input")) {
    libparen::init();
    libparen::print_logo();
    libparen::repl();
    printf("");
    return 0;
  }

  if (args.get<bool>("compile")) {
    assert(argc > 1);

    std::ofstream file_output;
    std::ostream &out = [&]() -> std::ostream & {
      if (!args.has("output")) {
        file_output = std::ofstream(args.get("input") + ".obj");
        return file_output;
      }

      if (args["output"] == "-")
        return std::cout;

      file_output = std::ofstream(args["output"]);
      return file_output;
    }();

    EmissionKind kind{EmissionKind::Object};
    if (args.get<bool>("emit-llvm"))
      kind = EmissionKind::IR;
    else if (args.get<bool>("emit-asm"))
      kind = EmissionKind::ASM;

    return Compile(args.get("input"), out, kind, args.getList("import"));
  }

  // execute files
  libparen::init();
  for (int i = 1; i < argc; i++) {
    std::string code;
    if (libparen::slurp(std::string(argv[i]), code)) {
      libparen::eval_string(code);
    } else {
      fprintf(stderr, "Cannot open file: %s\n", argv[i]);
    }
  }
}
