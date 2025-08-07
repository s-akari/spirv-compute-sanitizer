// a-b-c: 適当な float a[ARRAY_SIZE], b[ARRAY_SIZE], c[ARRAY_SIZE]
// を作成し、run(a, b, c) を呼び出します。a, b を入力、cを出力とする想定です。

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cl.h"

#define ARRAY_SIZE 8

typedef struct {
  float h_a[ARRAY_SIZE];
  float h_b[ARRAY_SIZE];
  float h_c[ARRAY_SIZE];
  cl_mem d_a;
  cl_mem d_b;
  cl_mem d_c;
} OpenCLBuffers;

int init_opencl_buffers(OpenCLContext *ctx, OpenCLBuffers *buffers) {
  cl_int err;

  for (size_t i = 0; i < ARRAY_SIZE; ++i) {
    buffers->h_a[i] = (float)i;
    buffers->h_b[i] = (float)i;
  }

  memset(buffers->h_c, 0, sizeof(buffers->h_c));

  const size_t buffer_size = sizeof(float) * ARRAY_SIZE;

  // Create buffers
  buffers->d_a =
      clCreateBuffer(ctx->context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                     buffer_size, buffers->h_a, &err);
  CHECK_CL_ERROR(err, "Error in creating buffer d_a");

  buffers->d_b =
      clCreateBuffer(ctx->context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                     buffer_size, buffers->h_b, &err);
  CHECK_CL_ERROR(err, "Error in creating buffer d_b");

  buffers->d_c =
      clCreateBuffer(ctx->context, CL_MEM_WRITE_ONLY, buffer_size, NULL, &err);
  CHECK_CL_ERROR(err, "Error in creating buffer d_c");

  return 0;
}

void clean_opencl_buffers(OpenCLBuffers *buffers) {
  if (buffers->d_a)
    clReleaseMemObject(buffers->d_a);
  if (buffers->d_b)
    clReleaseMemObject(buffers->d_b);
  if (buffers->d_c)
    clReleaseMemObject(buffers->d_c);

  memset(buffers, 0, sizeof(OpenCLBuffers));
}

void print_array(const char *name, const size_t size, const float array[]) {
  for (size_t i = 0; i < size; ++i) {
    printf("%s[%zu] = %f\n", name, i, array[i]);
  }
}

int run_kernel(OpenCLContext *ctx, OpenCLBuffers *buffers) {
  cl_int err;

  // Set kernel arguments
  err = clSetKernelArg(ctx->kernel, 0, sizeof(cl_mem), &buffers->d_a);
  CHECK_CL_ERROR(err, "Error in setting kernel argument d_a");

  err = clSetKernelArg(ctx->kernel, 1, sizeof(cl_mem), &buffers->d_b);
  CHECK_CL_ERROR(err, "Error in setting kernel argument d_b");

  err = clSetKernelArg(ctx->kernel, 2, sizeof(cl_mem), &buffers->d_c);
  CHECK_CL_ERROR(err, "Error in setting kernel argument d_c");

  // Enqueue kernel
  const size_t global_size = ARRAY_SIZE;

  err = clEnqueueNDRangeKernel(ctx->queue, ctx->kernel, 1, NULL, &global_size,
                               NULL, 0, NULL, NULL);
  CHECK_CL_ERROR(err, "Error in enqueueing kernel");

  // Wait for the kernel to finish
  err = clFinish(ctx->queue);
  CHECK_CL_ERROR(err, "Error in finishing command queue");

  // Read the output buffer
  err = clEnqueueReadBuffer(ctx->queue, buffers->d_c, CL_TRUE, 0,
                            sizeof(float) * ARRAY_SIZE, buffers->h_c, 0, NULL,
                            NULL);
  CHECK_CL_ERROR(err, "Error in reading output buffer d_c");

  return 0;
}

int main(const int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <path_to_spv_file>\n", argv[0]);

    return EXIT_FAILURE;
  }

  const char *path = argv[1];

  OpenCLContext ctx = {0};

  if (init_opencl_context(&ctx) != 0) {
    fprintf(stderr, "OpenCL context initialization failed.\n");

    clean_opencl_context(&ctx);

    return EXIT_FAILURE;
  }

  if (load_spv_program(&ctx, path, "run") != 0) {
    fprintf(stderr, "Cannot load SPIR-V program.\n");

    clean_opencl_context(&ctx);

    return EXIT_FAILURE;
  }

  OpenCLBuffers buffers = {0};

  if (init_opencl_buffers(&ctx, &buffers) != 0) {
    fprintf(stderr, "Failed to initialize OpenCL buffers.\n");

    clean_opencl_buffers(&buffers);
    clean_opencl_context(&ctx);

    return EXIT_FAILURE;
  }

  printf("Input arrays:\n");
  print_array("a", ARRAY_SIZE, buffers.h_a);
  printf("\n");
  print_array("b", ARRAY_SIZE, buffers.h_b);
  printf("\n\n");

  if (run_kernel(&ctx, &buffers) != 0) {
    fprintf(stderr, "Kernel execution failed.\n");

    clean_opencl_buffers(&buffers);
    clean_opencl_context(&ctx);

    return EXIT_FAILURE;
  }

  printf("Output array:\n");
  print_array("c", ARRAY_SIZE, buffers.h_c);

  clean_opencl_buffers(&buffers);
  clean_opencl_context(&ctx);

  return EXIT_SUCCESS;
}
