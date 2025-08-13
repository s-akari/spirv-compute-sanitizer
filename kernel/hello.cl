// hello: Hello World をします。

kernel void run(constant float *a, constant float *b, global float *c) {
  size_t gid = get_global_id(0);
  size_t lid = get_local_id(0);

  printf("Hello from Kernel #%zu, Local #%zu!\n", gid, lid);
}
