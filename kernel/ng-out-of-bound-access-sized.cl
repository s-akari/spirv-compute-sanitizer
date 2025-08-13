// ng-out-of-bound-access-sized: ホストから提供された配列のサイズを超えるアクセスを行い、不具合を誘発させます。各配列にサイズを付属させています。

kernel void run(constant float *a, const unsigned long a_size, constant float *b, const unsigned long b_size, global float *c, const unsigned long c_size) {
  size_t id = get_global_id(0);
  c[id] = a[id] + b[id + 3];
}
