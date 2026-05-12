/* ThorVG configuration for Aestra — vendored build */
/* Generated for CPU-only, SVG-only, no-threads, no-file-IO mode */

#ifndef THORVG_CONFIG_H
#define THORVG_CONFIG_H

#define THORVG_VERSION_STRING "1.0.0"

/* Disable threading — critical for DAW real-time safety */
/* #undef THORVG_THREAD_SUPPORT */

/* CPU rasterizer only */
#define THORVG_CPU_ENGINE_SUPPORT 1

/* Disable GPU engines */
/* #undef THORVG_GL_ENGINE_SUPPORT */
/* #undef THORVG_WG_ENGINE_SUPPORT */

/* SVG loader only */
#define THORVG_SVG_LOADER_SUPPORT 1

/* Disable other loaders */
/* #undef THORVG_PNG_LOADER_SUPPORT */
/* #undef THORVG_JPG_LOADER_SUPPORT */
/* #undef THORVG_LOTTIE_LOADER_SUPPORT */
/* #undef THORVG_SFNT_LOADER_SUPPORT */
/* #undef THORVG_TTF_LOADER_SUPPORT */
/* #undef THORVG_OTF_LOADER_SUPPORT */
/* #undef THORVG_WEBP_LOADER_SUPPORT */

/* Disable savers */
/* #undef THORVG_GIF_SAVER_SUPPORT */

/* Disable C API bindings */
/* #undef THORVG_CAPI_BINDING_SUPPORT */

/* Disable file IO — we load from memory buffers */
/* #undef THORVG_FILE_IO_SUPPORT */

/* Disable partial rendering */
/* #undef THORVG_PARTIAL_RENDER_SUPPORT */

/* Disable logging */
/* #undef THORVG_LOG_ENABLED */

/* Disable SIMD vectorization (avoid global AVX flags per AGENTS.md) */
/* #undef THORVG_AVX_VECTOR_SUPPORT */
/* #undef THORVG_NEON_VECTOR_SUPPORT */

/* Lottie extras disabled */
/* #undef THORVG_LOTTIE_EXPRESSIONS_SUPPORT */
/* #undef THORVG_OPENMP_SUPPORT */

/* Platform */
#define WIN32_LEAN_AND_MEAN 1

#endif /* THORVG_CONFIG_H */
