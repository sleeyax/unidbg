#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>

#include <sys/mman.h>

#include "dynarmic.h"
#include "arm_dynarmic_cp15.h"

static JavaVM* cachedJVM = NULL;
static jmethodID callSVC = NULL;
static jmethodID handleInterpreterFallback = NULL;

static void *get_memory(khash_t(memory) *memory, long vaddr) {
    long base = vaddr & ~PAGE_MASK;
    long off = vaddr - base;
    khiter_t k = kh_get(memory, memory, base);
    if(k == kh_end(memory)) {
      return NULL;
    }
    t_memory_page page = kh_value(memory, k);
    char *addr = (char *)page->addr;
    return &addr[off];
}

class DynarmicCallbacks32 final : public Dynarmic::A32::UserCallbacks {
public:
    DynarmicCallbacks32(khash_t(memory) *memory)
        : memory{memory}, cp15(std::make_shared<DynarmicCP15>()) {}

    ~DynarmicCallbacks32() = default;

    u32 MemoryReadCode(u32 vaddr) override {
        u32 code = MemoryRead32(vaddr);
        printf("MemoryReadCode[%s->%s:%d]: vaddr=0x%x, code=0x%x\n", __FILE__, __func__, __LINE__, vaddr, code);
        return code;
    }

    u8 MemoryRead8(u32 vaddr) override {
        u8 *dest = (u8 *) get_memory(memory, vaddr);
        if(dest) {
            return dest[0];
        } else {
            fprintf(stderr, "MemoryRead8[%s->%s:%d]: vaddr=0x%x\n", __FILE__, __func__, __LINE__, vaddr);
            abort();
            return 0;
        }
    }
    u16 MemoryRead16(u32 vaddr) override {
        if(vaddr & 1) {
            const u8 a{MemoryRead8(vaddr)};
            const u8 b{MemoryRead8(vaddr + sizeof(u8))};
            return (static_cast<u16>(b) << 8) | a;
        }
        u16 *dest = (u16 *) get_memory(memory, vaddr);
        if(dest) {
            return dest[0];
        } else {
            fprintf(stderr, "MemoryRead16[%s->%s:%d]: vaddr=0x%x\n", __FILE__, __func__, __LINE__, vaddr);
            abort();
            return 0;
        }
    }
    u32 MemoryRead32(u32 vaddr) override {
        if(vaddr & 3) {
            const u16 a{MemoryRead16(vaddr)};
            const u16 b{MemoryRead16(vaddr + sizeof(u16))};
            return (static_cast<u32>(b) << 16) | a;
        }
        u32 *dest = (u32 *) get_memory(memory, vaddr);
        if(dest) {
            return dest[0];
        } else {
            fprintf(stderr, "MemoryRead32[%s->%s:%d]: vaddr=0x%x\n", __FILE__, __func__, __LINE__, vaddr);
            abort();
            return 0;
        }
    }
    u64 MemoryRead64(u32 vaddr) override {
        if(vaddr & 7) {
            const u32 a{MemoryRead32(vaddr)};
            const u32 b{MemoryRead32(vaddr + sizeof(u32))};
            return (static_cast<u64>(b) << 32) | a;
        }
        u64 *dest = (u64 *) get_memory(memory, vaddr);
        if(dest) {
            return dest[0];
        } else {
            fprintf(stderr, "MemoryRead64[%s->%s:%d]: vaddr=0x%x\n", __FILE__, __func__, __LINE__, vaddr);
            abort();
            return 0;
        }
    }

    void MemoryWrite8(u32 vaddr, u8 value) override {
        u8 *dest = (u8 *) get_memory(memory, vaddr);
        if(dest) {
            dest[0] = value;
        } else {
            fprintf(stderr, "MemoryWrite8[%s->%s:%d]: vaddr=0x%x\n", __FILE__, __func__, __LINE__, vaddr);
            abort();
        }
    }
    void MemoryWrite16(u32 vaddr, u16 value) override {
        if(vaddr & 1) {
            MemoryWrite8(vaddr, static_cast<u8>(value));
            MemoryWrite8(vaddr + sizeof(u8), static_cast<u8>(value >> 8));
            return;
        }
        u16 *dest = (u16 *) get_memory(memory, vaddr);
        if(dest) {
            dest[0] = value;
        } else {
            fprintf(stderr, "MemoryWrite16[%s->%s:%d]: vaddr=0x%x\n", __FILE__, __func__, __LINE__, vaddr);
            abort();
        }
    }
    void MemoryWrite32(u32 vaddr, u32 value) override {
        if(vaddr & 3) {
            MemoryWrite16(vaddr, static_cast<u16>(value));
            MemoryWrite16(vaddr + sizeof(u16), static_cast<u16>(value >> 16));
            return;
        }
        u32 *dest = (u32 *) get_memory(memory, vaddr);
        if(dest) {
            dest[0] = value;
        } else {
            fprintf(stderr, "MemoryWrite32[%s->%s:%d]: vaddr=0x%x\n", __FILE__, __func__, __LINE__, vaddr);
            abort();
        }
    }
    void MemoryWrite64(u32 vaddr, u64 value) override {
        if(vaddr & 7) {
            MemoryWrite32(vaddr, static_cast<u32>(value));
            MemoryWrite32(vaddr + sizeof(u32), static_cast<u32>(value >> 32));
            return;
        }
        u64 *dest = (u64 *) get_memory(memory, vaddr);
        if(dest) {
            dest[0] = value;
        } else {
            fprintf(stderr, "MemoryWrite64[%s->%s:%d]: vaddr=0x%x\n", __FILE__, __func__, __LINE__, vaddr);
            abort();
        }
    }

    bool MemoryWriteExclusive8(u32 vaddr, u8 value, u8 expected) override {
        fprintf(stderr, "MemoryWriteExclusive8[%s->%s:%d]: vaddr=0x%x\n", __FILE__, __func__, __LINE__, vaddr);
        abort();
        return true;
    }
    bool MemoryWriteExclusive16(u32 vaddr, u16 value, u16 expected) override {
        MemoryWrite16(vaddr, value);
        return true;
    }
    bool MemoryWriteExclusive32(u32 vaddr, u32 value, u32 expected) override {
        MemoryWrite32(vaddr, value);
        return true;
    }
    bool MemoryWriteExclusive64(u32 vaddr, u64 value, u64 expected) override {
        MemoryWrite64(vaddr, value);
        return true;
    }

    void InterpreterFallback(u32 pc, std::size_t num_instructions) override {
        fprintf(stderr, "Unicorn fallback @ 0x%x for %lu instructions (instr = 0x%08X)", pc, num_instructions, MemoryReadCode(pc));
        abort();
    }

    void ExceptionRaised(u32 pc, Dynarmic::A32::Exception exception) override {
        fprintf(stderr, "ExceptionRaised[%s->%s:%d]: pc=0x%x, exception=%d\n", __FILE__, __func__, __LINE__, pc, exception);
        abort();
    }

    void CallSVC(u32 swi) override {
        JNIEnv *env;
        cachedJVM->AttachCurrentThread((void **)&env, NULL);
        env->CallVoidMethod(callback, callSVC, cpu->Regs()[15], swi);
        if (env->ExceptionCheck()) {
            cpu->HaltExecution();
        }
        cachedJVM->DetachCurrentThread();
    }

    void AddTicks(u64 ticks) override {
        this->ticks += ticks;
    }

    u64 GetTicksRemaining() override {
        return 0x10000000000;
    }

    u64 ticks = 0;
    khash_t(memory) *memory = NULL;
    jobject callback = NULL;
    std::shared_ptr<Dynarmic::A32::Jit> cpu;
    std::shared_ptr<DynarmicCP15> cp15;
};

class DynarmicCallbacks64 final : public Dynarmic::A64::UserCallbacks {

public:
    DynarmicCallbacks64(khash_t(memory) *memory)
        : memory{memory} {}

    ~DynarmicCallbacks64() = default;

    bool IsReadOnlyMemory(u64 vaddr) override {
        long base = vaddr & ~PAGE_MASK;
        khiter_t k = kh_get(memory, memory, base);
        if(k == kh_end(memory)) {
          return false;
        }
        t_memory_page page = kh_value(memory, k);
        return (page->perms & UC_PROT_WRITE) == 0;
    }

    u32 MemoryReadCode(u64 vaddr) override {
        u32 code = MemoryRead32(vaddr);
//        printf("MemoryReadCode[%s->%s:%d]: vaddr=%p, code=0x%x\n", __FILE__, __func__, __LINE__, (void*)vaddr, code);
        return code;
    }

    u8 MemoryRead8(u64 vaddr) override {
        u8 *dest = (u8 *) get_memory(memory, vaddr);
        if(dest) {
            return dest[0];
        } else {
            fprintf(stderr, "MemoryRead8[%s->%s:%d]: vaddr=%p\n", __FILE__, __func__, __LINE__, (void*)vaddr);
            abort();
            return 0;
        }
    }
    u16 MemoryRead16(u64 vaddr) override {
        if(vaddr & 1) {
            const u8 a{MemoryRead8(vaddr)};
            const u8 b{MemoryRead8(vaddr + sizeof(u8))};
            return (static_cast<u16>(b) << 8) | a;
        }
        u16 *dest = (u16 *) get_memory(memory, vaddr);
        if(dest) {
            return dest[0];
        } else {
            fprintf(stderr, "MemoryRead16[%s->%s:%d]: vaddr=%p\n", __FILE__, __func__, __LINE__, (void*)vaddr);
            abort();
            return 0;
        }
    }
    u32 MemoryRead32(u64 vaddr) override {
        if(vaddr & 3) {
            const u16 a{MemoryRead16(vaddr)};
            const u16 b{MemoryRead16(vaddr + sizeof(u16))};
            return (static_cast<u32>(b) << 16) | a;
        }
        u32 *dest = (u32 *) get_memory(memory, vaddr);
        if(dest) {
            return dest[0];
        } else {
            fprintf(stderr, "MemoryRead32[%s->%s:%d]: vaddr=%p\n", __FILE__, __func__, __LINE__, (void*)vaddr);
            abort();
            return 0;
        }
    }
    u64 MemoryRead64(u64 vaddr) override {
        if(vaddr & 7) {
            const u32 a{MemoryRead32(vaddr)};
            const u32 b{MemoryRead32(vaddr + sizeof(u32))};
            return (static_cast<u64>(b) << 32) | a;
        }
        u64 *dest = (u64 *) get_memory(memory, vaddr);
        if(dest) {
            return dest[0];
        } else {
            fprintf(stderr, "MemoryRead64[%s->%s:%d]: vaddr=%p\n", __FILE__, __func__, __LINE__, (void*)vaddr);
            abort();
            return 0;
        }
    }
    Dynarmic::A64::Vector MemoryRead128(u64 vaddr) override {
        return {MemoryRead64(vaddr), MemoryRead64(vaddr + 8)};
    }

    void MemoryWrite8(u64 vaddr, u8 value) override {
        u8 *dest = (u8 *) get_memory(memory, vaddr);
        if(dest) {
            dest[0] = value;
        } else {
            fprintf(stderr, "MemoryWrite8[%s->%s:%d]: vaddr=%p\n", __FILE__, __func__, __LINE__, (void*)vaddr);
            abort();
        }
    }
    void MemoryWrite16(u64 vaddr, u16 value) override {
        if(vaddr & 1) {
            MemoryWrite8(vaddr, static_cast<u8>(value));
            MemoryWrite8(vaddr + sizeof(u8), static_cast<u8>(value >> 8));
            return;
        }
        u16 *dest = (u16 *) get_memory(memory, vaddr);
        if(dest) {
            dest[0] = value;
        } else {
            fprintf(stderr, "MemoryWrite16[%s->%s:%d]: vaddr=%p\n", __FILE__, __func__, __LINE__, (void*)vaddr);
            abort();
        }

    }
    void MemoryWrite32(u64 vaddr, u32 value) override {
        if(vaddr & 3) {
            MemoryWrite16(vaddr, static_cast<u16>(value));
            MemoryWrite16(vaddr + sizeof(u16), static_cast<u16>(value >> 16));
            return;
        }
        u32 *dest = (u32 *) get_memory(memory, vaddr);
        if(dest) {
            dest[0] = value;
        } else {
            fprintf(stderr, "MemoryWrite32[%s->%s:%d]: vaddr=%p\n", __FILE__, __func__, __LINE__, (void*)vaddr);
            abort();
        }
    }
    void MemoryWrite64(u64 vaddr, u64 value) override {
        if(vaddr & 7) {
            MemoryWrite32(vaddr, static_cast<u32>(value));
            MemoryWrite32(vaddr + sizeof(u32), static_cast<u32>(value >> 32));
            return;
        }
        u64 *dest = (u64 *) get_memory(memory, vaddr);
        if(dest) {
            dest[0] = value;
        } else {
            fprintf(stderr, "MemoryWrite64[%s->%s:%d]: vaddr=%p\n", __FILE__, __func__, __LINE__, (void*)vaddr);
            abort();
        }
    }
    void MemoryWrite128(u64 vaddr, Dynarmic::A64::Vector value) override {
        MemoryWrite64(vaddr, value[0]);
        MemoryWrite64(vaddr + 8, value[1]);
    }

    bool MemoryWriteExclusive8(u64 vaddr, std::uint8_t value, std::uint8_t expected) override {
        fprintf(stderr, "MemoryWriteExclusive8[%s->%s:%d]: vaddr=%p\n", __FILE__, __func__, __LINE__, (void*)vaddr);
        abort();
        return true;
    }
    bool MemoryWriteExclusive16(u64 vaddr, std::uint16_t value, std::uint16_t expected) override {
        MemoryWrite16(vaddr, value);
        return true;
    }
    bool MemoryWriteExclusive32(u64 vaddr, std::uint32_t value, std::uint32_t expected) override {
        MemoryWrite32(vaddr, value);
        return true;
    }
    bool MemoryWriteExclusive64(u64 vaddr, std::uint64_t value, std::uint64_t expected) override {
        MemoryWrite64(vaddr, value);
        return true;
    }
    bool MemoryWriteExclusive128(u64 vaddr, Dynarmic::A64::Vector value, Dynarmic::A64::Vector expected) override {
        fprintf(stderr, "MemoryWriteExclusive128[%s->%s:%d]: vaddr=%p\n", __FILE__, __func__, __LINE__, (void*)vaddr);
        abort();
        return true;
    }

    void InterpreterFallback(u64 pc, std::size_t num_instructions) override {
        JNIEnv *env;
        cachedJVM->AttachCurrentThread((void **)&env, NULL);
        jboolean processed = env->CallBooleanMethod(callback, handleInterpreterFallback, pc, num_instructions);
        if (env->ExceptionCheck()) {
            cpu->HaltExecution();
        }
        if(processed == JNI_TRUE) {
            cpu->SetPC(pc + 4);
        } else {
            fprintf(stderr, "Unicorn fallback @ 0x%llx for %lu instructions (instr = 0x%08X)", pc, num_instructions, MemoryReadCode(pc));
            abort();
        }
        cachedJVM->DetachCurrentThread();
    }

    void ExceptionRaised(u64 pc, Dynarmic::A64::Exception exception) override {
        fprintf(stderr, "ExceptionRaised[%s->%s:%d]: pc=%p, exception=%d\n", __FILE__, __func__, __LINE__, (void*)pc, exception);
        abort();
    }

    void CallSVC(u32 swi) override {
        JNIEnv *env;
        cachedJVM->AttachCurrentThread((void **)&env, NULL);
        env->CallVoidMethod(callback, callSVC, cpu->GetPC(), swi);
        if (env->ExceptionCheck()) {
            cpu->HaltExecution();
        }
        cachedJVM->DetachCurrentThread();
    }

    void AddTicks(u64 ticks) override {
        this->ticks += ticks;
    }

    u64 GetTicksRemaining() override {
        return 0x10000000000;
    }

    u64 GetCNTPCT() override {
        return 0x10000000000;
    }

    u64 ticks = 0;
    u64 tpidrro_el0 = 0;
    u64 tpidr_el0 = 0;
    khash_t(memory) *memory = NULL;
    jobject callback = NULL;
    std::shared_ptr<Dynarmic::A64::Jit> cpu;
};

typedef struct dynarmic {
  bool is64Bit;
  khash_t(memory) *memory;
  std::shared_ptr<DynarmicCallbacks64> cb64;
  std::shared_ptr<Dynarmic::A64::Jit> jit64;
  std::shared_ptr<DynarmicCallbacks32> cb32;
  std::shared_ptr<Dynarmic::A32::Jit> jit32;
} *t_dynarmic;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Class:     com_github_unidbg_arm_backend_dynarmic_Dynarmic
 * Method:    setDynarmicCallback
 * Signature: (JLcom/github/unidbg/arm/backend/dynarmic/DynarmicCallback;)I
 */
JNIEXPORT jint JNICALL Java_com_github_unidbg_arm_backend_dynarmic_Dynarmic_setDynarmicCallback
  (JNIEnv *env, jclass clazz, jlong handle, jobject callback) {
  t_dynarmic dynarmic = (t_dynarmic) handle;
  if(dynarmic->is64Bit) {
    std::shared_ptr<DynarmicCallbacks64> cb = dynarmic->cb64;
    if(cb) {
      cb.get()->callback = env->NewGlobalRef(callback);
    } else {
      return 1;
    }
  } else {
    std::shared_ptr<DynarmicCallbacks32> cb = dynarmic->cb32;
    if(cb) {
      cb.get()->callback = env->NewGlobalRef(callback);
    } else {
      return 1;
    }
  }
  return 0;
}

/*
 * Class:     com_github_unidbg_arm_backend_dynarmic_Dynarmic
 * Method:    nativeInitialize
 * Signature: (Z)J
 */
JNIEXPORT jlong JNICALL Java_com_github_unidbg_arm_backend_dynarmic_Dynarmic_nativeInitialize
  (JNIEnv *env, jclass clazz, jboolean is64Bit) {
  t_dynarmic dynarmic = (t_dynarmic) calloc(1, sizeof(struct dynarmic));
  dynarmic->is64Bit = is64Bit == JNI_TRUE;
  dynarmic->memory = kh_init(memory);
  if(dynarmic->is64Bit) {
    std::shared_ptr<DynarmicCallbacks64> cb = std::make_shared<DynarmicCallbacks64>(dynarmic->memory);
    DynarmicCallbacks64 *callbacks = cb.get();

    Dynarmic::A64::UserConfig config;
    config.callbacks = callbacks;
    config.tpidrro_el0 = &callbacks->tpidrro_el0;
    config.tpidr_el0 = &callbacks->tpidr_el0;

    dynarmic->cb64 = cb;
    dynarmic->jit64 = std::make_shared<Dynarmic::A64::Jit>(config);
    callbacks->cpu = dynarmic->jit64;
  } else {
    std::shared_ptr<DynarmicCallbacks32> cb = std::make_shared<DynarmicCallbacks32>(dynarmic->memory);
    DynarmicCallbacks32 *callbacks = cb.get();

    Dynarmic::A32::UserConfig config;
    config.callbacks = callbacks;
    config.coprocessors[15] = callbacks->cp15;

    dynarmic->cb32 = cb;
    dynarmic->jit32 = std::make_shared<Dynarmic::A32::Jit>(config);
    callbacks->cpu = dynarmic->jit32;
  }
  return (jlong) dynarmic;
}

/*
 * Class:     com_github_unidbg_arm_backend_dynarmic_Dynarmic
 * Method:    nativeDestroy
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_com_github_unidbg_arm_backend_dynarmic_Dynarmic_nativeDestroy
  (JNIEnv *env, jclass clazz, jlong handle) {
  t_dynarmic dynarmic = (t_dynarmic) handle;
  khash_t(memory) *memory = dynarmic->memory;
  for (khiter_t k = kh_begin(memory); k < kh_end(memory); k++) {
    if(kh_exist(memory, k)) {
      t_memory_page page = kh_value(memory, k);
      int ret = munmap(page->addr, PAGE_SIZE);
      if(ret != 0) {
        fprintf(stderr, "munmap failed[%s->%s:%d]: addr=%p, ret=%d\n", __FILE__, __func__, __LINE__, page->addr, ret);
      }
      free(page);
    }
  }
  kh_destroy(memory, memory);
  std::shared_ptr<DynarmicCallbacks64> cb64 = dynarmic->cb64;
  if(cb64) {
    env->DeleteGlobalRef(cb64.get()->callback);
  }
  std::shared_ptr<DynarmicCallbacks32> cb32 = dynarmic->cb32;
  if(cb32) {
    env->DeleteGlobalRef(cb32.get()->callback);
  }
  free(dynarmic);
}

/*
 * Class:     com_github_unidbg_arm_backend_dynarmic_Dynarmic
 * Method:    mem_unmap
 * Signature: (JJJ)I
 */
JNIEXPORT jint JNICALL Java_com_github_unidbg_arm_backend_dynarmic_Dynarmic_mem_1unmap
  (JNIEnv *env, jclass clazz, jlong handle, jlong address, jlong size) {
  if(address & PAGE_MASK) {
    return 1;
  }
  if(size == 0 || (size & PAGE_MASK)) {
    return 2;
  }
  t_dynarmic dynarmic = (t_dynarmic) handle;
  khash_t(memory) *memory = dynarmic->memory;
  int ret;
  for(long vaddr = address; vaddr < address + size; vaddr += PAGE_SIZE) {
    khiter_t k = kh_get(memory, memory, vaddr);
    if(k == kh_end(memory)) {
      fprintf(stderr, "mem_unmap failed[%s->%s:%d]: vaddr=%p\n", __FILE__, __func__, __LINE__, (void*)vaddr);
      return 3;
    }
    t_memory_page page = kh_value(memory, k);
    int ret = munmap(page->addr, PAGE_SIZE);
    if(ret != 0) {
      fprintf(stderr, "munmap failed[%s->%s:%d]: addr=%p, ret=%d\n", __FILE__, __func__, __LINE__, page->addr, ret);
    }
    free(page);
    kh_del(memory, memory, k);
  }
  return 0;
}

/*
 * Class:     com_github_unidbg_arm_backend_dynarmic_Dynarmic
 * Method:    mem_map
 * Signature: (JJJI)I
 */
JNIEXPORT jint JNICALL Java_com_github_unidbg_arm_backend_dynarmic_Dynarmic_mem_1map
  (JNIEnv *env, jclass clazz, jlong handle, jlong address, jlong size, jint perms) {
  if(address & PAGE_MASK) {
    return 1;
  }
  if(size == 0 || (size & PAGE_MASK)) {
    return 2;
  }
  t_dynarmic dynarmic = (t_dynarmic) handle;
  khash_t(memory) *memory = dynarmic->memory;
  int ret;
  for(long vaddr = address; vaddr < address + size; vaddr += PAGE_SIZE) {
    if(kh_get(memory, memory, vaddr) != kh_end(memory)) {
      fprintf(stderr, "mem_map failed[%s->%s:%d]: vaddr=%p\n", __FILE__, __func__, __LINE__, (void*)vaddr);
      return 3;
    }
    void *addr = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if(addr == MAP_FAILED) {
      fprintf(stderr, "mmap failed[%s->%s:%d]: addr=%p\n", __FILE__, __func__, __LINE__, (void*)addr);
      return 4;
    }
    khiter_t k = kh_put(memory, memory, vaddr, &ret);
    t_memory_page page = (t_memory_page) calloc(1, sizeof(struct memory_page));
    page->addr = addr;
    page->perms = perms;
    kh_value(memory, k) = page;
  }
  return 0;
}

/*
 * Class:     com_github_unidbg_arm_backend_dynarmic_Dynarmic
 * Method:    mem_protect
 * Signature: (JJJI)I
 */
JNIEXPORT jint JNICALL Java_com_github_unidbg_arm_backend_dynarmic_Dynarmic_mem_1protect
  (JNIEnv *env, jclass clazz, jlong handle, jlong address, jlong size, jint perms) {
  if(address & PAGE_MASK) {
    return 1;
  }
  if(size == 0 || (size & PAGE_MASK)) {
    return 2;
  }
  t_dynarmic dynarmic = (t_dynarmic) handle;
  khash_t(memory) *memory = dynarmic->memory;
  int ret;
  for(long vaddr = address; vaddr < address + size; vaddr += PAGE_SIZE) {
    khiter_t k = kh_get(memory, memory, vaddr);
    if(k == kh_end(memory)) {
      fprintf(stderr, "mem_protect failed[%s->%s:%d]: vaddr=%p\n", __FILE__, __func__, __LINE__, (void*)vaddr);
      return 3;
    }
    t_memory_page page = kh_value(memory, k);
    page->perms = perms;
  }
  return 0;
}

/*
 * Class:     com_github_unidbg_arm_backend_dynarmic_Dynarmic
 * Method:    mem_write
 * Signature: (JJ[B)I
 */
JNIEXPORT jint JNICALL Java_com_github_unidbg_arm_backend_dynarmic_Dynarmic_mem_1write
  (JNIEnv *env, jclass clazz, jlong handle, jlong address, jbyteArray bytes) {
  jsize size = env->GetArrayLength(bytes);
  jbyte *data = env->GetByteArrayElements(bytes, NULL);
  t_dynarmic dynarmic = (t_dynarmic) handle;
  khash_t(memory) *memory = dynarmic->memory;
  char *src = (char *)data;
  long vaddr_end = address + size;
  for(long vaddr = address & ~PAGE_MASK; vaddr < vaddr_end; vaddr += PAGE_SIZE) {
    long start = vaddr < address ? address - vaddr : 0;
    long end = vaddr + PAGE_SIZE <= vaddr_end ? PAGE_SIZE : (vaddr_end - vaddr);
    long len = end - start;
    khiter_t k = kh_get(memory, memory, vaddr);
    if(k == kh_end(memory)) {
      fprintf(stderr, "mem_write failed[%s->%s:%d]: vaddr=%p\n", __FILE__, __func__, __LINE__, (void*)vaddr);
      return 1;
    }
    t_memory_page page = kh_value(memory, k);
    char *addr = (char *)page->addr;
    char *dest = &addr[start];
//    printf("mem_write address=%p, vaddr=%p, start=%ld, len=%ld, addr=%p, dest=%p\n", (void*)address, (void*)vaddr, start, len, addr, dest);
    memcpy(dest, src, len);
    src += len;
  }
  env->ReleaseByteArrayElements(bytes, data, JNI_ABORT);
  return 0;
}

/*
 * Class:     com_github_unidbg_arm_backend_dynarmic_Dynarmic
 * Method:    mem_read
 * Signature: (JJI)[B
 */
JNIEXPORT jbyteArray JNICALL Java_com_github_unidbg_arm_backend_dynarmic_Dynarmic_mem_1read
  (JNIEnv *env, jclass clazz, jlong handle, jlong address, jint size) {
  t_dynarmic dynarmic = (t_dynarmic) handle;
  khash_t(memory) *memory = dynarmic->memory;
  jbyteArray bytes = env->NewByteArray(size);
  long dest = 0;
  long vaddr_end = address + size;
  for(long vaddr = address & ~PAGE_MASK; vaddr < vaddr_end; vaddr += PAGE_SIZE) {
    long start = vaddr < address ? address - vaddr : 0;
    long end = vaddr + PAGE_SIZE <= vaddr_end ? PAGE_SIZE : (vaddr_end - vaddr);
    long len = end - start;
    khiter_t k = kh_get(memory, memory, vaddr);
    if(k == kh_end(memory)) {
      fprintf(stderr, "mem_read failed[%s->%s:%d]: vaddr=%p\n", __FILE__, __func__, __LINE__, (void*)vaddr);
      return NULL;
    }
    t_memory_page page = kh_value(memory, k);
    char *addr = (char *)page->addr;
    jbyte *src = (jbyte *)&addr[start];
    env->SetByteArrayRegion(bytes, dest, len, src);
    dest += len;
  }
  return bytes;
}

/*
 * Class:     com_github_unidbg_arm_backend_dynarmic_Dynarmic
 * Method:    reg_read_pc64
 * Signature: (J)J
 */
JNIEXPORT jlong JNICALL Java_com_github_unidbg_arm_backend_dynarmic_Dynarmic_reg_1read_1pc64
  (JNIEnv *env, jclass clazz, jlong handle) {
  t_dynarmic dynarmic = (t_dynarmic) handle;
  if(dynarmic->is64Bit) {
    std::shared_ptr<Dynarmic::A64::Jit> jit = dynarmic->jit64;
    if(jit) {
      return jit.get()->GetPC();
    } else {
      abort();
      return 1;
    }
  } else {
    abort();
    return -1;
  }
}

/*
 * Class:     com_github_unidbg_arm_backend_dynarmic_Dynarmic
 * Method:    reg_set_sp64
 * Signature: (JJ)I
 */
JNIEXPORT jint JNICALL Java_com_github_unidbg_arm_backend_dynarmic_Dynarmic_reg_1set_1sp64
  (JNIEnv *env, jclass clazz, jlong handle, jlong value) {
  t_dynarmic dynarmic = (t_dynarmic) handle;
  if(dynarmic->is64Bit) {
    std::shared_ptr<Dynarmic::A64::Jit> jit = dynarmic->jit64;
    if(jit) {
      jit.get()->SetSP(value);
    } else {
      return 1;
    }
  } else {
    return -1;
  }
  return 0;
}

/*
 * Class:     com_github_unidbg_arm_backend_dynarmic_Dynarmic
 * Method:    reg_read_sp64
 * Signature: (J)J
 */
JNIEXPORT jlong JNICALL Java_com_github_unidbg_arm_backend_dynarmic_Dynarmic_reg_1read_1sp64
  (JNIEnv *env, jclass clazz, jlong handle) {
  t_dynarmic dynarmic = (t_dynarmic) handle;
  if(dynarmic->is64Bit) {
    std::shared_ptr<Dynarmic::A64::Jit> jit = dynarmic->jit64;
    if(jit) {
      return jit.get()->GetSP();
    } else {
      abort();
      return 1;
    }
  } else {
    abort();
    return -1;
  }
}

/*
 * Class:     com_github_unidbg_arm_backend_dynarmic_Dynarmic
 * Method:    reg_set_tpidr_el0
 * Signature: (JJ)I
 */
JNIEXPORT jint JNICALL Java_com_github_unidbg_arm_backend_dynarmic_Dynarmic_reg_1set_1tpidr_1el0
  (JNIEnv *env, jclass clazz, jlong handle, jlong value) {
  t_dynarmic dynarmic = (t_dynarmic) handle;
  if(dynarmic->is64Bit) {
    std::shared_ptr<DynarmicCallbacks64> cb = dynarmic->cb64;
    if(cb) {
      cb.get()->tpidr_el0 = value;
    } else {
      return 1;
    }
  } else {
    return -1;
  }
  return 0;
}

/*
 * Class:     com_github_unidbg_arm_backend_dynarmic_Dynarmic
 * Method:    reg_write
 * Signature: (JIJ)I
 */
JNIEXPORT jint JNICALL Java_com_github_unidbg_arm_backend_dynarmic_Dynarmic_reg_1write
  (JNIEnv *env, jclass clazz, jlong handle, jint index, jlong value) {
  t_dynarmic dynarmic = (t_dynarmic) handle;
  if(dynarmic->is64Bit) {
    std::shared_ptr<Dynarmic::A64::Jit> jit = dynarmic->jit64;
    if(jit) {
      jit.get()->SetRegister(index, value);
    } else {
      return 1;
    }
  } else {
    std::shared_ptr<Dynarmic::A32::Jit> jit = dynarmic->jit32;
    if(jit) {
      jit.get()->Regs()[index] = (u32) value;
    } else {
      return 1;
    }
  }
  return 0;
}

/*
 * Class:     com_github_unidbg_arm_backend_dynarmic_Dynarmic
 * Method:    reg_read
 * Signature: (JI)J
 */
JNIEXPORT jlong JNICALL Java_com_github_unidbg_arm_backend_dynarmic_Dynarmic_reg_1read
  (JNIEnv *env, jclass clazz, jlong handle, jint index) {
  t_dynarmic dynarmic = (t_dynarmic) handle;
  if(dynarmic->is64Bit) {
    std::shared_ptr<Dynarmic::A64::Jit> jit = dynarmic->jit64;
    if(jit) {
      return jit.get()->GetRegister(index);
    } else {
      abort();
      return -1;
    }
  } else {
    std::shared_ptr<Dynarmic::A32::Jit> jit = dynarmic->jit32;
    if(jit) {
      return jit.get()->Regs()[index];
    } else {
      abort();
      return -1;
    }
  }
}

/*
 * Class:     com_github_unidbg_arm_backend_dynarmic_Dynarmic
 * Method:    emu_start
 * Signature: (JJ)I
 */
JNIEXPORT jint JNICALL Java_com_github_unidbg_arm_backend_dynarmic_Dynarmic_emu_1start
  (JNIEnv *env, jclass clazz, jlong handle, jlong pc) {
  t_dynarmic dynarmic = (t_dynarmic) handle;
  if(dynarmic->is64Bit) {
    std::shared_ptr<Dynarmic::A64::Jit> jit = dynarmic->jit64;
    if(jit) {
      std::shared_ptr<DynarmicCallbacks64> cb = dynarmic->cb64;
      Dynarmic::A64::Jit *cpu = jit.get();
      cpu->SetPC(pc);
      cpu->Run();
    } else {
      return 1;
    }
  } else {
    std::shared_ptr<Dynarmic::A32::Jit> jit = dynarmic->jit32;
    if(jit) {
      std::shared_ptr<DynarmicCallbacks32> cb = dynarmic->cb32;
      Dynarmic::A32::Jit *cpu = jit.get();
      bool thumb = pc & 1;
      if(pc & 1) {
        cpu->SetCpsr(0x00000030); // Thumb mode
      } else {
        cpu->SetCpsr(0x00000000); // Arm mode
      }
      cpu->Regs()[15] = (u32) (pc & ~1);
      cpu->Run();
    } else {
      return 1;
    }
  }
  return 0;
}

/*
 * Class:     com_github_unidbg_arm_backend_dynarmic_Dynarmic
 * Method:    emu_stop
 * Signature: (J)I
 */
JNIEXPORT jint JNICALL Java_com_github_unidbg_arm_backend_dynarmic_Dynarmic_emu_1stop
  (JNIEnv *env, jclass clazz, jlong handle) {
  t_dynarmic dynarmic = (t_dynarmic) handle;
  if(dynarmic->is64Bit) {
    std::shared_ptr<Dynarmic::A64::Jit> jit = dynarmic->jit64;
    if(jit) {
      Dynarmic::A64::Jit *cpu = jit.get();
      cpu->HaltExecution();
    } else {
      return 1;
    }
  } else {
    return -1;
  }
  return 0;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);

  JNIEnv *env;
  if (JNI_OK != vm->GetEnv((void **)&env, JNI_VERSION_1_6)) {
    return JNI_ERR;
  }
  jclass cDynarmicCallback = env->FindClass("com/github/unidbg/arm/backend/dynarmic/DynarmicCallback");
  if (env->ExceptionCheck()) {
    return JNI_ERR;
  }
  callSVC = env->GetMethodID(cDynarmicCallback, "callSVC", "(JI)V");
  handleInterpreterFallback = env->GetMethodID(cDynarmicCallback, "handleInterpreterFallback", "(JI)Z");
  cachedJVM = vm;

  return JNI_VERSION_1_6;
}

#ifdef __cplusplus
}
#endif
