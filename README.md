# SPIR-V Compute Sanitizer

SPIR-V にコンパイルされたOpenCL カーネルのバイナリを検証するツールを作成する試み。
CUDAの [Compute Sanitizer](https://docs.nvidia.com/compute-sanitizer/ComputeSanitizer/index.html)
と似たようなものをSPIR-Vで実現することを目指します。

## 使い方

必要なもの (Arch Linux パッケージ):

- `clang` (>= 14)
- `spirv-tools`
- `spirv-llvm-translator`
- `opencl-headers`
- (Intel GPUなら) `intel-compute-runtime`
- (AMD GPUなら) `opencl-mesa`
- (NVIDIA GPUなら) `opencl-nvidia`
- (必要に応じて) `clinfo`

`clinfo | grep SPIR`で出力があれば利用できます。AMDの場合、`RUSTICL_ENABLE=radeonsi`を実行時につけないと動かない場合があるかもしれません。

### 構造

ソースコードは以下の3つのディレクトリに分かれています。

- `common`: `runner`から使用される共通の実装
- `kernel`: OpenCL カーネルのソースコード
- `plugin`: Sanitizer 埋込用 LLVM Pass Plugin
- `runtime`: Sanitizer ランタイムライブラリ
- `runner`: OpenCL カーネルを実行するためのランナー (Cの単一ソースファイル)

### ビルド・実行

全体のビルド:

```bash
make
```

それぞれのカーネルやランナーがどのようなものかは、ソースコードの最初のコメントを参照してください。

カーネルの個別ビルド:

個別ビルドの前に、`make build-runtime` でランタイムライブラリのビルドをする必要があります。

```bash
make build-runtime
make build-kernel/<kernel_name>

# 例:
make build-kernel/add # kernel/add.cl -> out/kernel/add.spv
```

`out/kernel/`にビルドされたカーネルのSPIR-Vバイナリが出力されます。

ランナーの個別ビルド:

```bash
make build-runner/<runner_name>

# 例:
make build-runner/a-b-c # runner/a-b-c.c -> out/bin/a-b-c
```

`out/bin/`にビルドされたランナーの実行ファイルが出力されます。

実行:

```bash
<runner> <kernel>

# 例:
out/bin/a-b-c out/kernel/add.spv
```

### Sanitizer の機能

#### ASan: Array index out of bounds

カーネルの引数に配列ポインターとサイズ (`unsigned long`) が横並びで指定されていれば、自動で判別し境界チェックを行います。

```c
kernel void run(constant float *a, const unsigned long a_size, constant float *b, const unsigned long b_size, global float *c, const unsigned long c_size) {
  unsigned long id = get_global_id(0);

  c[id] = a[id] + b[id + 3]; // :bomb:
}
```

#### TSan: Local memory conflict

カーネル関数内でローカルメモリバッファが作られていた場合、自動で認識し競合チェックを行います。

```c
kernel void run(global int* input, global int* output, const unsigned long size) {
  local int local_buffer[256];

  int gid = get_global_id(0);
  int lid = get_local_id(0);
  int group_size = get_local_size(0);

  local_buffer[lid % 32] = input[gid]; // :bomb:
}
```

## 参考リンク

- https://docs.nvidia.com/compute-sanitizer/ComputeSanitizer/index.html
- https://github.com/NVIDIA/compute-sanitizer-samples
