// add: c = a + b を計算します。

kernel void run(constant float *a, const unsigned long a_size, constant float *b, const unsigned long b_size, global float *c, const unsigned long c_size) {
  unsigned long id = get_global_id(0);
  c[id] = a[id] + b[id];
}
