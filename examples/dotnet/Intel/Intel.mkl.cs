#pragma warning disable CS8981

using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;

public static partial class Intel {
    public unsafe static partial class mkl {
        static readonly IntPtr mkl_rt;

        public struct MKLVersion {
            public int MajorVersion;
            public int MinorVersion;
            public int UpdateVersion;
            public byte* ProductStatus;
            public byte* Build;
            public byte* Processor;
            public byte* Platform;
        }

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        delegate void MKL_Get_Version(out MKLVersion version);

        public static MKLVersion? Version;

        static mkl() {
            string StringFromPChar(byte* pSz) {
                int len = 0;
                for (int i = 0; i < 1024; i++) {
                    if (pSz[i] == 0) {
                        break;
                    }
                    len++;
                }
                return Encoding.UTF8.GetString(pSz, len);
            }
            Version = null;
            if (kernel32.LoadLibraryW("c:\\python312\\library\\bin\\mkl_rt.2.dll", out mkl_rt)) {
                Debug.Write("\n> Found " + kernel32.GetModuleFileName(mkl_rt) + "\n");
                if (!kernel32.GetProcAddress(mkl_rt, "MKL_Get_Version", out MKL_Get_Version mkl_get_version)) {
                    goto error;
                }
                mkl_get_version(out MKLVersion version);
                Debug.Write($"> Major version = {version.MajorVersion}\n");
                Debug.Write($"> Minor version = {version.MinorVersion}\n");
                Debug.Write($"> Update version = {version.UpdateVersion}\n");
                Debug.Write($"> Product status = {StringFromPChar(version.ProductStatus)}\n");
                Debug.Write($"> Build = {StringFromPChar(version.Build)}\n");
                Debug.Write($"> Platform = {StringFromPChar(version.Platform)}\n\n");
                Version = version;
                if (!kernel32.GetProcAddress(mkl_rt, "cblas_sdot", out sdot)) {
                    goto error;
                }
                if (!kernel32.GetProcAddress(mkl_rt, "cblas_sscal", out sscal)) {
                    goto error;
                }
                if (!kernel32.GetProcAddress(mkl_rt, "cblas_sgemm", out sgemm)) {
                    goto error;
                }
                if (!kernel32.GetProcAddress(mkl_rt, "cblas_sgemv", out sgemv)) {
                    goto error;
                }
                if (!kernel32.GetProcAddress(mkl_rt, "cblas_saxpy", out saxpy)) {
                    goto error;
                }
                if (!kernel32.GetProcAddress(mkl_rt, "vsTanh", out tanh)) {
                    goto error;
                }
                return;
            }
        error:
            Version = null;
            if (mkl_rt != IntPtr.Zero) {
                Debug.Write("> \u001b[33mWARNING: Version not supported.\u001b[0m\n");
                kernel32.FreeLibrary(mkl_rt);
                mkl_rt = IntPtr.Zero;
            }
            tanh = (n, a, y) => throw new NotSupportedException($"Not found '{nameof(tanh)}': Intel® oneAPI Math Kernel Library.");
            sdot = error_sdot;
            sscal = error_sscal;
            saxpy = error_saxpy;
            sgemv = error_sgemv;
            sgemm = error_sgemm;
        }

        public static bool IsSupported => mkl_rt != IntPtr.Zero && Version != null;

        [System.Security.SuppressUnmanagedCodeSecurity]
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate void vsTanh(int N, float* a, float* y);

        /// <see href="https://www.intel.com/content/www/us/en/docs/onemkl/developer-reference-c/2024-0/v-tanh.html"></see>
        public static readonly vsTanh tanh;

        /// <summary>
        /// x = a * x
        /// </summary>
        /// <see href="https://www.intel.com/content/www/us/en/docs/onemkl/developer-reference-c/2024-1/cblas-scal.html"></see>
        public static readonly CBLAS_SSCAL sscal;

        /// <summary>
        /// y := a * x + y
        /// </summary>
        /// <see href="https://www.intel.com/content/www/us/en/docs/onemkl/developer-reference-c/2024-1/cblas-axpy.html"></see>
        public static readonly CBLAS_SAXPY saxpy;

        /// <summary>
        /// y := alpha * A * x + beta * y
        /// </summary>
        /// <see href="https://www.intel.com/content/www/us/en/docs/onemkl/developer-reference-c/2024-1/cblas-gemv.html"></see>
        public static readonly CBLAS_SGEMV sgemv;

        /// <summary>
        /// C := alpha* op(A)*op(B) + beta* C
        /// </summary>
        /// <see href="https://www.intel.com/content/www/us/en/docs/onemkl/developer-reference-c/2024-1/cblas-gemm-001.html"></see>
        public static readonly CBLAS_SGEMM sgemm;

        /// <summary>
        /// Computes a vector-vector dot product.
        /// </summary>
        /// <see href="https://www.intel.com/content/www/us/en/docs/onemkl/developer-reference-c/2024-1/cblas-dot.html"></see>
        public static readonly CBLAS_SDOT sdot;

        unsafe static void error_sscal(int N, float a, float* x, int incx) {
            throw new NotSupportedException($"Not found '{nameof(sscal)}': Intel® oneAPI Math Kernel Library.");
        }

        unsafe static float error_sdot(int N, float* x, int incx, float* y, int incy) {
            throw new NotSupportedException($"Not found '{nameof(sdot)}': Intel® oneAPI Math Kernel Library.");
        }

        unsafe static void error_saxpy(int N, float a, float* x, int incx, float* y, int incy) {
            throw new NotSupportedException($"Not found '{nameof(saxpy)}': Intel® oneAPI Math Kernel Library.");
        }

        unsafe static void error_sgemv(CBLAS_LAYOUT Layout, CBLAS_TRANSPOSE Trans, int M, int N, float alpha, float* A, int lda, float* x, int incx, float beta, float* y, int incy) {
            throw new NotSupportedException($"Not found '{nameof(sgemv)}': Intel® oneAPI Math Kernel Library.");
        }

        unsafe static void error_sgemm(CBLAS_LAYOUT Layout, CBLAS_TRANSPOSE TransA, CBLAS_TRANSPOSE TransB, int M, int N, int K, float alpha, float* A, int lda, float* B, int ldb, float beta, float* C, int ldc) {
            throw new NotSupportedException($"Not found '{nameof(sgemm)}': Intel® oneAPI Math Kernel Library.");
        }
    }
}
