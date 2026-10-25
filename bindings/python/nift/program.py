"""Owned Runtime 2.0 programs over ABI 1.4; independent of Engine's legacy state.

Use a context manager (or close) on the compiling thread. Native programs must
be closed before that thread exits. Result objects copy all borrowed data.
"""
import ctypes as C
from dataclasses import dataclass
import threading


class _String(C.Structure):
    _fields_ = [('data', C.c_void_p), ('length', C.c_size_t)]


class _Source(C.Structure):
    _fields_ = [('kind', C.c_int), ('data', C.c_void_p), ('length', C.c_size_t),
                ('name', C.c_void_p), ('name_length', C.c_size_t)]


@dataclass(frozen=True)
class ProgramResult:
    outcome: int
    output: bytes
    return_text: str
    returned: bool
    message: str
    code: str
    source: str
    line: int
    column: int
    catchable: bool

    @property
    def ok(self):
        return self.outcome == 0


class ProgramCompileError(ValueError):
    def __init__(self, result):
        self.result = result
        super().__init__(f'{result.source}:{result.line}:{result.column}: {result.message}')


def _library(path):
    if path is None:
        from . import _nift
        path = _nift.__file__
    lib = C.CDLL(str(path))
    signatures = {
        'compile': ([C.POINTER(_Source), C.POINTER(C.c_void_p), C.POINTER(C.c_void_p)], C.c_int),
        'free': ([C.c_void_p], C.c_int),
        'run': ([C.c_void_p, C.c_size_t, C.c_size_t, C.c_size_t, C.POINTER(C.c_void_p)], C.c_int),
        'result_free': ([C.c_void_p], None),
    }
    for name, (args, result) in signatures.items():
        fn = getattr(lib, 'nift_program_' + name)
        fn.argtypes, fn.restype = args, result
    for name in ('output', 'return_text', 'error_message', 'error_code', 'error_source'):
        fn = getattr(lib, 'nift_program_result_' + name)
        fn.argtypes, fn.restype = [C.c_void_p, C.POINTER(_String)], C.c_int
    for name in ('outcome', 'returned', 'catchable', 'error_line', 'error_column'):
        fn = getattr(lib, 'nift_program_result_' + name)
        fn.argtypes = [C.c_void_p]
        fn.restype = C.c_ulonglong if name.startswith('error_') else C.c_int
    return lib


def _check(status):
    if status:
        raise RuntimeError(f'Nift program ABI call failed (status {status})')


def _result(lib, handle):
    try:
        def data(name):
            view = _String()
            _check(getattr(lib, 'nift_program_result_' + name)(handle, C.byref(view)))
            return C.string_at(view.data, view.length)
        return ProgramResult(lib.nift_program_result_outcome(handle), data('output'),
                             data('return_text').decode('utf-8'),
                             bool(lib.nift_program_result_returned(handle)),
                             data('error_message').decode('utf-8'), data('error_code').decode('utf-8'),
                             data('error_source').decode('utf-8'),
                             lib.nift_program_result_error_line(handle),
                             lib.nift_program_result_error_column(handle),
                             bool(lib.nift_program_result_catchable(handle)))
    finally:
        lib.nift_program_result_free(handle)


class Program:
    """Compile once, run fresh VM instances; no fallback or Engine bindings.

    source accepts str or bytes. library optionally names a standalone ABI 1.4
    shared library; by default the installed native extension supplies the ABI.
    """
    def __init__(self, source, logical_name='<embed>', *, library=None):
        self._lib = _library(library)
        self._owner = threading.current_thread()
        self._handle = C.c_void_p()
        text = source.encode('utf-8') if isinstance(source, str) else bytes(source)
        name = logical_name.encode('utf-8')
        text_buffer, name_buffer = C.create_string_buffer(text), C.create_string_buffer(name)
        raw = _Source(0, C.cast(text_buffer, C.c_void_p), len(text),
                      C.cast(name_buffer, C.c_void_p), len(name))
        result = C.c_void_p()
        _check(self._lib.nift_program_compile(C.byref(raw), C.byref(self._handle), C.byref(result)))
        try:
            self.compilation = _result(self._lib, result)
            if not self.compilation.ok:
                raise ProgramCompileError(self.compilation)
        except BaseException:
            self.close()
            raise

    def _check(self):
        if threading.current_thread() is not self._owner:
            raise RuntimeError('Program must be used and closed on the compiling thread')
        if not self._handle.value:
            raise RuntimeError('Program is closed')

    def run(self, *, max_frames=0, max_registers=0, heap_budget=0):
        self._check()
        for value in (max_frames, max_registers, heap_budget):
            if not isinstance(value, int) or not 0 <= value <= C.c_size_t(-1).value:
                raise ValueError('VM limits must fit size_t and be non-negative')
        if 0 < max_registers < 256:
            raise ValueError('max_registers must be zero or at least 256')
        result = C.c_void_p()
        _check(self._lib.nift_program_run(self._handle, max_frames, max_registers,
                                         heap_budget, C.byref(result)))
        return _result(self._lib, result)

    def close(self):
        if self._handle.value:
            self._check()
            _check(self._lib.nift_program_free(self._handle))
            self._handle = C.c_void_p()

    def __enter__(self):
        self._check()
        return self

    def __exit__(self, *args):
        self.close()

    def __del__(self):
        # Explicit close is required before owner-thread exit. Never destroy
        # thread-local VM values on another thread during garbage collection.
        if (getattr(self, '_handle', None) and
                threading.current_thread() is getattr(self, '_owner', None)):
            self.close()
