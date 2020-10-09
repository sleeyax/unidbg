#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>

#include <sys/mman.h>
#include "dynarmic.h"

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

class DynarmicCallbacks64 final : public Dynarmic::A64::UserCallbacks {
public:
    u8 MemoryRead8(u64 vaddr) override {
        fprintf(stderr, "MemoryRead8[%s->%s:%d]: vaddr=%p\n", __FILE__, __func__, __LINE__, (void*)vaddr);
        abort();
        return 0;
    }
    u16 MemoryRead16(u64 vaddr) override {
        fprintf(stderr, "MemoryRead16[%s->%s:%d]: vaddr=%p\n", __FILE__, __func__, __LINE__, (void*)vaddr);
        abort();
        return 0;
    }
    u32 MemoryRead32(u64 vaddr) override {
        fprintf(stderr, "MemoryRead32[%s->%s:%d]: vaddr=%p\n", __FILE__, __func__, __LINE__, (void*)vaddr);
        abort();
        return 0;
    }
    u64 MemoryRead64(u64 vaddr) override {
        fprintf(stderr, "MemoryRead64[%s->%s:%d]: vaddr=%p\n", __FILE__, __func__, __LINE__, (void*)vaddr);
        abort();
        return 0;
    }
    Dynarmic::A64::Vector MemoryRead128(u64 vaddr) override {
        return {MemoryRead64(vaddr), MemoryRead64(vaddr + 8)};
    }

    void MemoryWrite8(u64 vaddr, u8 value) override {
        fprintf(stderr, "MemoryWrite8[%s->%s:%d]: vaddr=%p\n", __FILE__, __func__, __LINE__, (void*)vaddr);
        abort();
    }
    void MemoryWrite16(u64 vaddr, u16 value) override {
        fprintf(stderr, "MemoryWrite16[%s->%s:%d]: vaddr=%p\n", __FILE__, __func__, __LINE__, (void*)vaddr);
        abort();
    }
    void MemoryWrite32(u64 vaddr, u32 value) override {
        fprintf(stderr, "MemoryWrite32[%s->%s:%d]: vaddr=%p\n", __FILE__, __func__, __LINE__, (void*)vaddr);
        abort();
    }
    void MemoryWrite64(u64 vaddr, u64 value) override {
        fprintf(stderr, "MemoryWrite64[%s->%s:%d]: vaddr=%p\n", __FILE__, __func__, __LINE__, (void*)vaddr);
        abort();
    }
    void MemoryWrite128(u64 vaddr, Dynarmic::A64::Vector value) override {
        MemoryWrite64(vaddr, value[0]);
        MemoryWrite64(vaddr + 8, value[1]);
    }

    bool MemoryWriteExclusive8(u64 vaddr, std::uint8_t value, std::uint8_t expected) override {
        fprintf(stderr, "MemoryWriteExclusive8[%s->%s:%d]: vaddr=%p\n", __FILE__, __func__, __LINE__, (void*)vaddr);
        abort();
        return false;
    }
    bool MemoryWriteExclusive16(u64 vaddr, std::uint16_t value, std::uint16_t expected) override {
        fprintf(stderr, "MemoryWriteExclusive16[%s->%s:%d]: vaddr=%p\n", __FILE__, __func__, __LINE__, (void*)vaddr);
        abort();
        return false;
    }
    bool MemoryWriteExclusive32(u64 vaddr, std::uint32_t value, std::uint32_t expected) override {
        fprintf(stderr, "MemoryWriteExclusive32[%s->%s:%d]: vaddr=%p\n", __FILE__, __func__, __LINE__, (void*)vaddr);
        abort();
        return false;
    }
    bool MemoryWriteExclusive64(u64 vaddr, std::uint64_t value, std::uint64_t expected) override {
        fprintf(stderr, "MemoryWriteExclusive64[%s->%s:%d]: vaddr=%p\n", __FILE__, __func__, __LINE__, (void*)vaddr);
        abort();
        return false;
    }
    bool MemoryWriteExclusive128(u64 vaddr, Dynarmic::A64::Vector value, Dynarmic::A64::Vector expected) override {
        fprintf(stderr, "MemoryWriteExclusive128[%s->%s:%d]: vaddr=%p\n", __FILE__, __func__, __LINE__, (void*)vaddr);
        abort();
        return false;
    }

    void InterpreterFallback(u64 pc, std::size_t num_instructions) override {
        fprintf(stderr, "InterpreterFallback[%s->%s:%d]: pc=%p, num_instructions=%lu\n", __FILE__, __func__, __LINE__, (void*)pc, num_instructions);
        abort();
    }

    void ExceptionRaised(u64 pc, Dynarmic::A64::Exception exception) override {
        fprintf(stderr, "ExceptionRaised[%s->%s:%d]: pc=%p, exception=%d\n", __FILE__, __func__, __LINE__, (void*)pc, exception);
        abort();
    }

    void CallSVC(u32 swi) override {
        fprintf(stderr, "CallSVC[%s->%s:%d]: swi=%d\n", __FILE__, __func__, __LINE__, swi);
        abort();
    }

    void AddTicks(u64 ticks) override {
    }

    u64 GetTicksRemaining() override {
        return (u64) -1;
    }

    u64 GetCNTPCT() override {
        return 0;
    }
};

static u64 tpidrro_el0 = 0;
static u64 tpidr_el0 = 0;

#ifdef __cplusplus
extern "C" {
#endif

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
    DynarmicCallbacks64 callbacks;
    Dynarmic::A64::UserConfig config;
    config.callbacks = &callbacks;
    config.tpidrro_el0 = &tpidrro_el0;
    config.tpidr_el0 = &tpidr_el0;
    Dynarmic::A64::Jit jit{config};
    dynarmic->jit64 = &jit;
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
  free(dynarmic);
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

#ifdef __cplusplus
}
#endif
