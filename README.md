# SPIR-V Compute Sanitizer

SPIR-V にコンパイルされたOpenCL カーネルのバイナリを検証するツールを作成する試み。
CUDAの [Compute Sanitizer](https://docs.nvidia.com/compute-sanitizer/ComputeSanitizer/index.html)
と似たようなものをSPIR-Vで実現することを目指します。

とりあえず今はSPIR-Vのバイナリを読み込んで動かすだけのものです。

## 使い方

必要なもの (Arch Linux パッケージ):

- `clang` (>= 14)
- `spirv-tools`
- `opencl-headers`
- (Intel GPUなら) `intel-compute-runtime`
- (AMD GPUなら) `opencl-mesa`
- (NVIDIA GPUなら) `opencl-nvidia`
- (必要に応じて) `clinfo`

`clinfo | grep SPIR`で出力があれば利用できます。AMDの場合、`RUSTICL_ENABLE=radeonsi`を実行時につけないと動かない場合があるかもしれません。

### 構造

ソースコードは以下の2つのディレクトリに分かれています。

- `kernel`: OpenCL カーネルのソースコード
- `runner`: OpenCL カーネルを実行するためのランナー (Cの単一ソースファイル)

### ビルド・実行

全体のビルド:

```bash
make
```

カーネルの個別ビルド:

```bash
make build-kernel/<kernel_name>

# 例:
make build-kernel/add # out/kernel/add.spv
```

`out/kernel/`にビルドされたカーネルのSPIR-Vバイナリが出力されます。

ランナーの個別ビルド:

```bash
make build-runner/<runner_name>

# 例:
make build-runner/a-b-c # out/bin/a-b-c
```

`out/bin/`にビルドされたランナーの実行ファイルが出力されます。

実行:

```bash
<runner_name> <kernel_name>

# 例:
out/bin/a-b-c out/kernel/add.spv
```

## 参考リンク

https://docs.nvidia.com/compute-sanitizer/ComputeSanitizer/index.html
https://github.com/NVIDIA/compute-sanitizer-samples
