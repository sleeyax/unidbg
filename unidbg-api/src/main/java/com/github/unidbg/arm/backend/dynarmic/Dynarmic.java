package com.github.unidbg.arm.backend.dynarmic;

import com.github.unidbg.utils.Inspector;
import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;

import java.io.Closeable;
import java.io.IOException;
import java.util.Arrays;

public class Dynarmic implements Closeable {

    static {
        try {
            org.scijava.nativelib.NativeLoader.loadLibrary("dynarmic");
        } catch (IOException e) {
            throw new IllegalStateException(e);
        }
    }

    private static final Log log = LogFactory.getLog(Dynarmic.class);

    private static native long nativeInitialize(boolean is64Bit);
    private static native void nativeDestroy(long handle);

    private static native int mem_unmap(long handle, long address, long size);
    private static native int mem_map(long handle, long address, long size, int perms);
    private static native int mem_protect(long handle, long address, long size, int perms);

    private static native int mem_write(long handle, long address, byte[] bytes);

    private static native int reg_set_sp64(long handle, long value);
    private static native int reg_set_tpidr_el0(long handle, long value);

    private static native int reg_write(long handle, int index, long value);

    private final long nativeHandle;

    public Dynarmic(boolean is64Bit) {
        this.nativeHandle = nativeInitialize(is64Bit);
    }

    public void mem_unmap(long address, long size) {
        long start = log.isDebugEnabled() ? System.currentTimeMillis() : 0;
        int ret = mem_unmap(nativeHandle, address, size);
        if (log.isDebugEnabled()) {
            log.debug("mem_unmap address=0x" + Long.toHexString(address) + ", size=0x" + Long.toHexString(size) + ", offset=" + (System.currentTimeMillis() - start) + "ms");
        }
        if (ret != 0) {
            throw new DynarmicException("ret=" + ret);
        }
    }

    public void mem_map(long address, long size, int perms) {
        long start = log.isDebugEnabled() ? System.currentTimeMillis() : 0;
        int ret = mem_map(nativeHandle, address, size, perms);
        if (log.isDebugEnabled()) {
            log.debug("mem_map address=0x" + Long.toHexString(address) + ", size=0x" + Long.toHexString(size) + ", perms=0b" + Integer.toBinaryString(perms) + ", offset=" + (System.currentTimeMillis() - start) + "ms");
        }
        if (ret != 0) {
            throw new DynarmicException("ret=" + ret);
        }
    }

    public void mem_protect(long address, long size, int perms) {
        long start = log.isDebugEnabled() ? System.currentTimeMillis() : 0;
        int ret = mem_protect(nativeHandle, address, size, perms);
        if (log.isDebugEnabled()) {
            log.debug("mem_protect address=0x" + Long.toHexString(address) + ", size=0x" + Long.toHexString(size) + ", perms=0b" + Integer.toBinaryString(perms) + ", offset=" + (System.currentTimeMillis() - start) + "ms");
        }
        if (ret != 0) {
            throw new DynarmicException("ret=" + ret);
        }
    }

    public void reg_set_sp64(long value) {
        if (log.isDebugEnabled()) {
            log.debug("reg_set_sp64 value=0x" + Long.toHexString(value));
        }
        int ret = reg_set_sp64(nativeHandle, value);
        if (ret != 0) {
            throw new DynarmicException("ret=" + ret);
        }
    }

    public void reg_set_tpidr_el0(long value) {
        if (log.isDebugEnabled()) {
            log.debug("reg_set_tpidr_el0 value=0x" + Long.toHexString(value));
        }
        int ret = reg_set_tpidr_el0(nativeHandle, value);
        if (ret != 0) {
            throw new DynarmicException("ret=" + ret);
        }
    }

    public void reg_write64(int index, long value) {
        if (index < 0 || index > 30) {
            throw new IllegalArgumentException("index=" + index);
        }
        if (log.isDebugEnabled()) {
            log.debug("reg_write64 index=" + index + ", value=0x" + Long.toHexString(value));
        }
        int ret = reg_write(nativeHandle, index, value);
        if (ret != 0) {
            throw new DynarmicException("ret=" + ret);
        }
    }

    public void mem_write(long address, byte[] bytes) {
        long start = log.isDebugEnabled() ? System.currentTimeMillis() : 0;
        int ret = mem_write(nativeHandle, address, bytes);
        if (log.isDebugEnabled()) {
            log.debug("mem_write address=0x" + Long.toHexString(address) + ", size=" + bytes.length + ", offset=" + (System.currentTimeMillis() - start) + "ms");
        }
        if (ret != 0) {
            throw new DynarmicException("ret=" + ret);
        }
    }

    @Override
    public void close() {
        nativeDestroy(nativeHandle);
    }
}