// a-b-c: 適当な float a[ARRAY_SIZE], b[ARRAY_SIZE], c[ARRAY_SIZE]
// を作成し、run(a, b, c) を呼び出します。a, b を入力、cを出力とする想定です。

#include <stdio.h>
#include <stdlib.h>

#include <CL/cl.h>

#define ARRAY_SIZE 8

cl_int errcode;

void set_range_float_array(const size_t size, float array[]) {
  for (size_t i = 0; i < size; ++i) {
    array[i] = (float)i;
  }
}

void print_array(const char *name, const size_t size, const float array[]) {
  for (size_t i = 0; i < size; ++i) {
    printf("%s[%zu] = %f\n", name, i, array[i]);
  }
}

void create_spv_program(const char *path, const cl_context *context,
                        cl_program *program) {
  FILE *il_file = fopen(path, "rb");

  if (!il_file) {
    fprintf(stderr, "Failed to open file: %s\n", path);

    exit(EXIT_FAILURE);
  }

  // Get file size
  fseek(il_file, 0, SEEK_END);

  const long _il_size = ftell(il_file);

  if (_il_size < 0) {
    fprintf(stderr, "Failed to get file size: %s\n", path);

    fclose(il_file);
    exit(EXIT_FAILURE);
  }

  const size_t il_size = (size_t)_il_size;

  fseek(il_file, 0, SEEK_SET);

  char *il = malloc(il_size);

  if (!il) {
    fprintf(stderr, "Failed to allocate memory for SPIR-V file\n");

    fclose(il_file);
    exit(EXIT_FAILURE);
  }

  const size_t read_size = fread(il, 1, il_size, il_file);

  if (read_size != il_size) {
    printf("Failed to read SPIR-V file: %s\n", path);

    fclose(il_file);
    free(il);
    exit(EXIT_FAILURE);
  }

  fclose(il_file);

  *program = clCreateProgramWithIL(*context, il, il_size, &errcode);

  if (errcode != CL_SUCCESS) {
    printf("Error in creating program\n");

    free(il);
    exit(EXIT_FAILURE);
  }

  free(il);
}

int main(const int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <path_to_spv_file>\n", argv[0]);

    return EXIT_FAILURE;
  }

  const char *path = argv[1];

  // Get platform id
  cl_platform_id platform_id;
  cl_uint platform_num;
  clGetPlatformIDs(1, &platform_id, &platform_num);

  // Get device id
  cl_device_id device_id;
  clGetDeviceIDs(platform_id, CL_DEVICE_TYPE_DEFAULT, 1, &device_id, NULL);

  // Create context
  cl_context context;
  context = clCreateContext(0, 1, &device_id, NULL, NULL, &errcode);
  if (errcode != CL_SUCCESS) {
    fprintf(stderr, "Failed to create OpenCL context: %d\n", errcode);

    return EXIT_FAILURE;
  }

  cl_command_queue commands;
  commands =
      clCreateCommandQueueWithProperties(context, device_id, NULL, &errcode);
  if (errcode != CL_SUCCESS) {
    fprintf(stderr, "Failed to create command queue: %d\n", errcode);

    return EXIT_FAILURE;
  }

  cl_program program;
  create_spv_program(path, &context, &program);

  errcode = clBuildProgram(program, 0, NULL, NULL, NULL, NULL);
  if (errcode != CL_SUCCESS) {
    fprintf(stderr, "Error in building program: %d\n", errcode);

    return EXIT_FAILURE;
  }

  size_t info_size;
  clGetProgramInfo(program, CL_PROGRAM_KERNEL_NAMES, 0, NULL, &info_size);
  char *info = malloc(info_size + 1);

  clGetProgramInfo(program, CL_PROGRAM_KERNEL_NAMES, info_size, info, NULL);

  info[info_size] = '\0'; // Ensure null-termination

  printf("Program kernel names: %s\n", info);

  free(info);

  cl_kernel ko_run = clCreateKernel(program, "run", &errcode);
  if (errcode != CL_SUCCESS) {
    fprintf(stderr, "Error in creating kernel: %d\n", errcode);

    return EXIT_FAILURE;
  }

  float h_a[ARRAY_SIZE], h_b[ARRAY_SIZE], h_c[ARRAY_SIZE];

  set_range_float_array(ARRAY_SIZE, h_a);
  set_range_float_array(ARRAY_SIZE, h_b);

  printf("Input arrays:\n");
  print_array("a", ARRAY_SIZE, h_a);
  printf("\n");
  print_array("b", ARRAY_SIZE, h_b);
  printf("\n\n");

  // Create buffers
  cl_mem d_a = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                              sizeof(float) * ARRAY_SIZE, h_a, NULL);

  cl_mem d_b = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                              sizeof(float) * ARRAY_SIZE, h_b, NULL);

  cl_mem d_c = clCreateBuffer(context, CL_MEM_WRITE_ONLY,
                              sizeof(float) * ARRAY_SIZE, NULL, NULL);

  // Set kernel arguments
  clSetKernelArg(ko_run, 0, sizeof(cl_mem), &d_a);
  clSetKernelArg(ko_run, 1, sizeof(cl_mem), &d_b);
  clSetKernelArg(ko_run, 2, sizeof(cl_mem), &d_c);

  // Enqueue kernel
  clEnqueueNDRangeKernel(commands, ko_run, 1, NULL, (size_t[]){ARRAY_SIZE},
                         NULL, 0, NULL, NULL);

  clFinish(commands);

  clEnqueueReadBuffer(commands, d_c, CL_TRUE, 0, sizeof(float) * ARRAY_SIZE,
                      h_c, 0, NULL, NULL);

  printf("Output array:\n");
  print_array("c", ARRAY_SIZE, h_c);

  clReleaseMemObject(d_a);
  clReleaseMemObject(d_b);
  clReleaseMemObject(d_c);
  clReleaseProgram(program);
  clReleaseKernel(ko_run);
  clReleaseCommandQueue(commands);
  clReleaseContext(context);

  return EXIT_SUCCESS;
}
