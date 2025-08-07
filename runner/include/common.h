#pragma once

#include <CL/cl.h>

#define CL_ERROR(err, msg)                                                     \
  do {                                                                         \
    fprintf(stderr, "OpenCL error (%d): %s\n", err, msg);                      \
    return -1;                                                                 \
  } while (0)

#define CHECK_CL_ERROR(err, msg)                                               \
  do {                                                                         \
    if (err != CL_SUCCESS) {                                                   \
      CL_ERROR(err, msg);                                                      \
    }                                                                          \
  } while (0)

typedef struct {
  cl_context context;
  cl_command_queue queue;
  cl_program program;
  cl_kernel kernel;
  cl_device_id device;
} OpenCLContext;

int init_opencl_context(OpenCLContext *ctx) {
  cl_int err;
  cl_platform_id platform;

  err = clGetPlatformIDs(1, &platform, NULL);
  CHECK_CL_ERROR(err, "Failed to get OpenCL platform");

  err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &ctx->device, NULL);
  CHECK_CL_ERROR(err, "Failed to get OpenCL device");

  ctx->context = clCreateContext(NULL, 1, &ctx->device, NULL, NULL, &err);
  CHECK_CL_ERROR(err, "Failed to create OpenCL context");

  ctx->queue =
      clCreateCommandQueueWithProperties(ctx->context, ctx->device, NULL, &err);
  CHECK_CL_ERROR(err, "Failed to create command queue");

  return 0;
}

void clean_opencl_context(OpenCLContext *ctx) {
  if (ctx->kernel)
    clReleaseKernel(ctx->kernel);
  if (ctx->program)
    clReleaseProgram(ctx->program);
  if (ctx->queue)
    clReleaseCommandQueue(ctx->queue);
  if (ctx->context)
    clReleaseContext(ctx->context);

  memset(ctx, 0, sizeof(OpenCLContext));
}

int load_spv_program(OpenCLContext *ctx, const char *path,
                     const char *kernel_name) {
  cl_int err;

  if (ctx->program) {
    fprintf(stderr, "Program already exists.");

    return -1;
  }

  // Load SPIR-V file into memory
  FILE *il_file = fopen(path, "rb");

  if (!il_file) {
    fprintf(stderr, "Failed to open file: %s\n", path);

    return -1;
  }

  // Get file size
  fseek(il_file, 0, SEEK_END);

  const long _il_size = ftell(il_file);

  if (_il_size < 0) {
    fprintf(stderr, "Failed to get file size: %s\n", path);

    fclose(il_file);

    return -1;
  }

  const size_t il_size = (size_t)_il_size;

  fseek(il_file, 0, SEEK_SET);

  char *il = malloc(il_size);

  if (!il) {
    fprintf(stderr, "Failed to allocate memory for SPIR-V file\n");

    fclose(il_file);

    return -1;
  }

  const size_t read_size = fread(il, 1, il_size, il_file);

  if (read_size != il_size) {
    printf("Failed to read SPIR-V file: %s\n", path);

    fclose(il_file);
    free(il);

    return -1;
  }

  fclose(il_file);

  // Create program
  ctx->program = clCreateProgramWithIL(ctx->context, il, il_size, &err);
  free(il);
  CHECK_CL_ERROR(err, "Error in creating program");

  err = clBuildProgram(ctx->program, 0, NULL, NULL, NULL, NULL);
  if (err != CL_SUCCESS) {
    size_t log_size;

    clGetProgramBuildInfo(ctx->program, ctx->device, CL_PROGRAM_BUILD_LOG, 0,
                          NULL, &log_size);

    char *log = malloc(log_size);

    if (!log) {
      fprintf(stderr, "Log allocation failed\n");

      return -1;
    }

    fprintf(stderr, "OpenCL error (%d): Failed to build program: %s\n", err,
            log);

    free(log);

    return -1;
  }

  size_t info_size;
  err = clGetProgramInfo(ctx->program, CL_PROGRAM_KERNEL_NAMES, 0, NULL,
                         &info_size);
  CHECK_CL_ERROR(err, "Failed to get program info");

  char *info = malloc(info_size + 1);

  if (!info) {
    fprintf(stderr, "Info allocation failed\n");

    return -1;
  }

  err = clGetProgramInfo(ctx->program, CL_PROGRAM_KERNEL_NAMES, info_size, info,
                         NULL);
  if (err != CL_SUCCESS) {
    free(info);

    CL_ERROR(err, "Failed to get program info");
  }

  info[info_size] = '\0'; // Ensure null-termination

  printf("Program kernel names: %s\n", info);

  free(info);

  ctx->kernel = clCreateKernel(ctx->program, kernel_name, &err);
  CHECK_CL_ERROR(err, "Error in creating kernel");

  return 0;
}
