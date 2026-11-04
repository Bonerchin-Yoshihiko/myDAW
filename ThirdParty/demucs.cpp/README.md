# demucs.cpp (vendored)

C++17 inference for the Demucs v4 hybrid-transformer music source separation model,
by Sevag H, MIT licensed (see `LICENSE`).

- Upstream: https://github.com/sevagh/demucs.cpp
- Commit: f1206e9adeea103aef4a636b9e62297cf1f8e34e (only `src/` is included)
- Local changes:
  - `model_apply.cpp` uses a fixed time-shift offset instead of `rand()` (thread safety and
    reproducible results).
  - The segment / STFT buffers are zero-initialised (`model.hpp`, `dsp.hpp`).
  - Tensor shapes use `Eigen::array<Eigen::Index, N>` instead of `Eigen::array<int, N>` so the
    code compiles with MSVC (no change in results).
- Model weights (not included): https://huggingface.co/datasets/Retrobear/demucs.cpp
  (converted from Meta's Demucs, MIT licensed)

myDAW downloads the model on first use, or loads a model file you pick.
