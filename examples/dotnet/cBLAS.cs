// https://en.wikipedia.org/wiki/Basic_Linear_Algebra_Subprograms

[System.Security.SuppressUnmanagedCodeSecurity]
[System.Runtime.InteropServices.UnmanagedFunctionPointer(System.Runtime.InteropServices.CallingConvention.Cdecl)]
public unsafe delegate void CBLAS_SSCAL(int N, float a, float* x, int incx);

[System.Security.SuppressUnmanagedCodeSecurity]
[System.Runtime.InteropServices.UnmanagedFunctionPointer(System.Runtime.InteropServices.CallingConvention.Cdecl)]
public unsafe delegate void CBLAS_SAXPY(int N, float a, float* x, int incx, float* y, int incy);

/// <summary>
/// Computes a vector-vector dot product.
/// </summary>
/// <param name="N">Specifies the number of elements in vectors x and y.</param>
/// <param name="x">Array, size at least (1+(n-1)*abs(incx)).</param>
/// <param name="incx">Specifies the increment for the elements of x.</param>
/// <param name="y">Array, size at least (1+(n-1)*abs(incy)).</param>
/// <param name="incy">Specifies the increment for the elements of y.</param>
/// <returns>The result of the dot product of x and y, if n is positive. Otherwise, returns 0.</returns>
[System.Security.SuppressUnmanagedCodeSecurity]
[System.Runtime.InteropServices.UnmanagedFunctionPointer(System.Runtime.InteropServices.CallingConvention.Cdecl)]
public unsafe delegate float CBLAS_SDOT(int N, float* x, int incx, float* y, int incy);

public enum CBLAS_LAYOUT : int {
    RowMajor = 101,
    ColMajor = 102
};

public enum CBLAS_TRANSPOSE : int {
    NoTrans = 111,
    Trans = 112,
    /*ConjTrans = 113*/
};

[System.Security.SuppressUnmanagedCodeSecurity]
[System.Runtime.InteropServices.UnmanagedFunctionPointer(System.Runtime.InteropServices.CallingConvention.Cdecl)]
public unsafe delegate void CBLAS_SGEMV(
    CBLAS_LAYOUT Layout,
    CBLAS_TRANSPOSE TransA,
    int M,
    int N,
    float alpha,
    float* A,
    int lda,
    float* X,
    int incX,
    float beta,
    float* Y,
    int incY);

[System.Security.SuppressUnmanagedCodeSecurity]
[System.Runtime.InteropServices.UnmanagedFunctionPointer(System.Runtime.InteropServices.CallingConvention.Cdecl)]
public unsafe delegate void CBLAS_SGEMM(
    CBLAS_LAYOUT Layout,
    CBLAS_TRANSPOSE TransA,
    CBLAS_TRANSPOSE TransB,
    int M,
    int N,
    int K,
    float alpha,
    float* A,
    int lda,
    float* B,
    int ldb,
    float beta,
    float* C,
    int ldc);

public unsafe static partial class cBLAS {
    public const CBLAS_LAYOUT CblasColMajor = CBLAS_LAYOUT.ColMajor;
    public const CBLAS_LAYOUT CblasRowMajor = CBLAS_LAYOUT.RowMajor;

    public const CBLAS_TRANSPOSE CblasNoTrans = CBLAS_TRANSPOSE.NoTrans;
    public const CBLAS_TRANSPOSE CblasTrans = CBLAS_TRANSPOSE.Trans;
}

public unsafe static partial class cBLAS {
    public static bool IsSupported => true;
}