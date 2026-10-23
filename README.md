# HowlGuard for macOS

실시간 하울링(피드백) 억제기. 단독 실행 앱(.app), VST3, Audio Unit으로 빌드됩니다.
사용법은 [docs/USER_GUIDE_KO_MAC.md](docs/USER_GUIDE_KO_MAC.md) 를 보세요.

## 맥에서 직접 빌드하기

1. 터미널에서 `xcode-select --install` (Xcode Command Line Tools, 한 번만)
2. [Homebrew](https://brew.sh) 설치 후 `brew install cmake` (한 번만)
3. 이 폴더에서 `bash scripts/build_mac.sh`

처음 빌드할 때 JUCE 프레임워크를 인터넷에서 받아오므로 10~20분 걸릴 수 있습니다.
결과물은 `dist/HowlGuard-1.2.0-mac.zip` 에 생깁니다 (Apple Silicon + Intel 유니버설).

## GitHub에서 자동 빌드

이 폴더를 GitHub 저장소에 올리면 `.github/workflows/build-mac.yml` 이 GitHub의 맥 서버에서
자동으로 빌드합니다. 저장소의 **Actions** 탭 → 최신 실행 → **Artifacts** 에서 zip을 받으면 됩니다.

## 구조

- `Source/dsp/FeedbackDetector.*` : 170 ms FFT 분석, 순음/고조파/지속성/증가율로 하울링 판정
- `Source/ChannelEngine.*` : 채널별 노치 32개(자동/고정, 복원, 1분 기억), 컴프레서, 미터
- `Source/PluginProcessor.*` : 단독 실행은 최대 16채널을 채널마다 따로, 플러그인은 1채널(스테레오 링크)
- `Source/PluginEditor.*`, `Source/ui/*` : 화면
