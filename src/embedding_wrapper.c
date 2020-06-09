// This file is a part of Julia. License is MIT: http://julialang.org/license

// Standard headers
#include <string.h>
#include <stdint.h>
#include <stdio.h>

// Julia headers (for initialization and gc commands)
#include "uv.h"
#include "julia.h"

#ifdef _MSC_VER
JL_DLLEXPORT char *dirname(char *);
#else
#include <libgen.h>
#endif

#define cast_to_char_ptr_ptr(X) ((char**)(X))

JULIA_DEFINE_FAST_TLS()

// Declare C prototype of a function defined in Julia
int julia_main(jl_array_t*);

#ifndef _OS_WINDOWS_
int main(int argc, char *argv[])
{
    uv_setup_args(argc, argv); // no-op on Windows
#else

void hide_console_window() {
    HWND hWnd = GetConsoleWindow();
    ShowWindow( hWnd, SW_MINIMIZE );
    ShowWindow( hWnd, SW_HIDE );
}

int wmain(int argc, wchar_t *argv[], wchar_t *envp[])
{
    // write the command line to UTF8
    for (int i = 0; i < argc; i++) { 
        wchar_t *warg = argv[i];
        size_t len = WideCharToMultiByte(CP_UTF8, 0, warg, -1, NULL, 0, NULL, NULL);
        if (!len) return 1;
        char *arg = (char*)alloca(len);
        if (!WideCharToMultiByte(CP_UTF8, 0, warg, -1, arg, len, NULL, NULL)) return 1;
        argv[i] = (wchar_t*)arg;
    }

    hide_console_window();
#endif

    // initialization
    libsupport_init();

    // Get the current exe path so we can compute a relative depot path
    char *free_path = (char*)malloc(PATH_MAX);
    size_t path_size = PATH_MAX;
    if (!free_path)
       jl_errorf("fatal error: failed to allocate memory: %s", strerror(errno));
    if (uv_exepath(free_path, &path_size)) {
       jl_error("fatal error: unexpected error while retrieving exepath");
    }
 
    char buf[PATH_MAX];
    snprintf(buf, sizeof(buf), "JULIA_DEPOT_PATH=%s/", dirname(dirname(free_path)));
    putenv(buf);
    putenv("JULIA_LOAD_PATH=@");

    // JULIAC_PROGRAM_LIBNAME defined on command-line for compilation
    jl_options.image_file = JULIAC_PROGRAM_LIBNAME;
    julia_init(JL_IMAGE_JULIA_HOME);

    // Initialize Core.ARGS with the full argv.
    jl_set_ARGS(argc, cast_to_char_ptr_ptr(argv));

    // Set PROGRAM_FILE to argv[0].
    jl_set_global(jl_base_module,
        jl_symbol("PROGRAM_FILE"), (jl_value_t*)jl_cstr_to_string(cast_to_char_ptr_ptr(argv)[0]));

    // Set Base.ARGS to `String[ unsafe_string(argv[i]) for i = 1:argc ]`
    jl_array_t *ARGS = (jl_array_t*)jl_get_global(jl_base_module, jl_symbol("ARGS"));
    jl_array_grow_end(ARGS, argc - 1);
    for (int i = 1; i < argc; i++) {
        jl_value_t *s = (jl_value_t*)jl_cstr_to_string(cast_to_char_ptr_ptr(argv)[i]);
        jl_arrayset(ARGS, s, i - 1);
    }

    // call the work function, and get back a value
    int retcode = julia_main(ARGS);

    // Cleanup and gracefully exit
    jl_atexit_hook(retcode);
    return retcode;
}
