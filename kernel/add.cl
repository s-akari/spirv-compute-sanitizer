// add: c = a + b を計算します。

kernel void run(constant float *a, constant float *b, global float *c) {
  int id = get_global_id(0);
  c[id] = a[id] + b[id];
}
