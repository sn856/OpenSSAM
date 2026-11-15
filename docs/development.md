# Development Notes

초안 문서. 현재는 UI 개선 작업만 정리한다.

## 목적

- 이 문서는 `modern/` 경로에 있는 OpenSSAM 작업본을 기준으로 작성한다.
- 레거시 SSAM GUI의 사용성을 개선한다.
- 창 크기 변경 시 내부 UI가 자연스럽게 반응하도록 만든다.
- 원본 보관본은 유지하고, 모든 OpenSSAM 수정은 `modern/`에서 진행한다.

## 현재 확인된 문제

- 메인 창은 크기 조절이 가능하지만 내부 탭과 컨트롤 배치가 유동적으로 따라오지 않는다.
- 탭 내부 UI는 대부분 고정 좌표 기반으로 배치되어 있다.
- 현재 메인 다이얼로그는 초기화 시점에만 탭 영역 크기를 계산하고, 리사이즈 시 재배치를 수행하지 않는다.

## 작업 대상

- OpenSSAM GUI 작업 경로: `modern/SSAM3/SSAMAPP/`
- `modern/SSAM3/SSAMAPP/`
- 우선 확인 파일:
  - `SSAMAPPDlg.cpp`
  - `SSAMAPPDlg.h`
  - `SSAMAPP.rc`
  - 각 탭 다이얼로그 cpp/h 파일

## UI 개선 우선순위

1. 메인 창 리사이즈 대응
2. 탭 컨트롤과 하단 버튼 재배치
3. 현재 탭 페이지 크기 재계산
4. `Conflicts`, `Summary`, `Map` 탭의 주요 리스트/뷰 영역 가변 배치
5. `Configure` 탭의 입력 영역 정리

## 작업 원칙

- `archive/`는 수정하지 않는다.
- 기능 변경보다 레이아웃 안정화와 사용성 개선을 먼저 한다.
- 한 번에 전체 UI를 갈아엎지 않고, 메인 창 -> 탭 -> 세부 컨트롤 순서로 진행한다.
- 기존 동작을 최대한 유지하면서 화면 배치만 개선한다.
- 새 UI 실험은 가능하지만, 최소한 현재 기능 흐름은 계속 사용할 수 있어야 한다.

## 1차 목표

- 창 크기를 늘리거나 줄여도 탭 영역이 함께 늘어나야 한다.
- 하단 `Save`, `Open`, `About`, `Close` 버튼이 창 크기에 맞춰 적절히 유지되어야 한다.
- 활성 탭 페이지가 메인 창 크기에 맞춰 다시 배치되어야 한다.

## 빌드 전략

- 현재 기준 공식 개발 타깃은 `x64`만 지원한다.
- `x86` 빌드와 `archive/SSAM3EXE_x86/` 기반 배포는 현재 계획에서 제외한다.
- 기본 실행본은 멀티스레드 버전으로 잡는다.
- 기준 빌드 구성:
  - `SSAMDLL`: `Release_MT | x64`
  - `SSAMAPP`: `Release_MT | x64`
- 기준 실행 파일은 `SSAM3M.exe`로 본다.
- 기준 분석 DLL은 `SSAMDLL_MT.dll`로 본다.
- 개발 중 실행 검증은 `modern/SSAM3/WorkingDir/`에서 수행한다.
- 사용자 배포본은 최종적으로 `SSAM3EXE` 형태의 단일 폴더로 제공한다.
- 빌드와 배포는 분리한다.
  - 빌드 결과 확인: `WorkingDir`
  - 사용자 배포 패키지: 별도 `SSAM3EXE`
- 초기 목표는 "최신 수정 사항이 반영된 `SSAM3M.exe`를 `SSAM3EXE` 폴더 형태로 안정적으로 배포할 수 있는 상태"를 만드는 것이다.

## 현재 빌드 상태

- 현재 로컬 빌드 기준은 `Visual Studio Build Tools 2022 + Windows SDK 10.0.22621 + MSVC v143`이다.
- `SSAMDLL.sln`은 `Release_MT | x64` 기준으로 빌드 가능하다.
- `SSAMAPP.sln`도 `Release_MT | x64` 기준으로 빌드 가능하다.
- `SSAM3M.exe`는 `modern/SSAM3/WorkingDir/`에 생성된다.
- `SSAMDLL_MT.dll`은 `modern/SSAMDLL/bin/`에 생성되며, 실행 전에 `WorkingDir`로 복사해 맞춰야 한다.
- 현재 OSG 개발 헤더/라이브러리는 저장소 루트의 `.deps/` 아래에 로컬 호환 계층으로 정리한다.
- `archive/SSAM3EXE/`는 최종 배포 폴더 템플릿으로 사용한다.

## 빌드 및 실행 메모

- 소스 수정 사항을 반영하려면 다시 빌드해야 한다.
- 현재 UI 수정은 주로 `modern/SSAM3/SSAMAPP/` 쪽이지만, 실행 세트를 맞추려면 `SSAMDLL`과 `SSAMAPP`을 함께 보는 것이 안전하다.
- 권장 빌드 순서:
  1. `modern/SSAMDLL/SSAMDLL.sln`
  2. `modern/SSAM3/SSAMAPP.sln`
- 주요 출력 위치:
  - `modern/SSAMDLL/bin/`
  - `modern/SSAM3/WorkingDir/`
  - 디버그는 `modern/SSAM3/WorkingDirDebug/`
- `WorkingDir`는 개발 중 실행 확인용 폴더로 본다.
- 사용자가 바로 실행할 배포본은 나중에 `WorkingDir` 기반으로 별도 `SSAM3EXE` 형태로 패키징한다.
- 현재 주 빌드 대상은 `SSAM3M.exe` + `SSAMDLL_MT.dll` 조합이다.
- `SSAM3.exe` / `SSAM3M.exe`는 단독 실행 파일이 아니라 같은 폴더의 `SSAMDLL*.dll`, OSG DLL, `fonts/`, `tables/` 등을 함께 필요로 한다.
- `x64` 실행 구성은 `archive/SSAM3EXE/`를 참고한다.
- 현재 단계에서는 `x86` 호환성을 고려하지 않는다.
- 현재 기준 OpenSceneGraph 개발 의존성은 저장소 내부 `.deps/` 경로를 사용한다.
- 반복 작업은 수동 빌드보다 스크립트 실행을 우선한다.

## 자동화 스크립트

- 빌드:
  - `pwsh -File scripts/build-release-mt-x64.ps1`
- 패키징:
  - `pwsh -File scripts/package-ssam3exe.ps1`
- 패키징 결과:
  - 기본 출력 경로는 `dist/SSAM3EXE/`
- 패키징 방식:
  - `archive/SSAM3EXE/`를 템플릿으로 복사한 뒤, 새 `SSAM3M.exe`와 `SSAMDLL_MT.dll`을 덮어쓴다.
  - 사용자 혼동을 줄이기 위해 `SSAM3.exe`도 현재 `SSAM3M.exe`와 동일한 멀티스레드 빌드로 덮어쓴다.
  - 템플릿에 들어 있던 기존 `SSAM3EXE.zip`은 패키지에서 제외한다.
- 이유:
  - 사용자가 받는 폴더 구성을 가능한 한 원본 `SSAM3EXE`와 비슷하게 유지하기 위함이다.

## 다음 확인 사항

- 메인 다이얼로그에 `OnSize`를 추가할지 여부
- 탭별 공통 리사이즈 처리 함수를 둘지 여부
- MFC 고정 배치 유지 + 수동 재배치 방식으로 갈지, 간단한 레이아웃 헬퍼를 도입할지 여부

## 분석 엔진 속도 개선 (2026-11)

### 결과

- 분석 결과(결과 CSV, TRJ 텍스트 파일, P(UEA)·mTTC·mPET 값)는 수정 전과 바이트 단위로 같다.

| TRJ 파일 크기 | 수정 전 | 수정 후 |
|---|---|---|
| 약 150MB | 2.5초 | 0.4초 |
| 약 6GB | 130초 | 19초 |

### 바뀐 것

- 파일을 4MB 덩어리로 읽는다 (리틀엔디언 파일만. 빅엔디언 파일은 예전 방식).
- 차량 기록 보관 방식을 바꿨다 (시간 단계별 번호 지도 → 정렬된 번호 목록).
- 읽기 전용 스레드가 분석과 동시에 파일을 읽는다. 앞서 읽는 시간 단계는 최대 64개다.
- 상충 후보 찾기(투영 계산, 겹침 검사)와 차량 연결을 여러 스레드로 나눈다. TTC·PET 계산과 상충 확정은 예전처럼 한 스레드에서 순서대로 한다 (P(UEA) 난수 순서를 지키기 위해).
- 분석 스레드 수 기본값: `min(6, 논리 프로세서 수 / 2)`, 최소 1. 화면의 칸이나 `nthreads=`에 적은 값이 우선한다. 읽기 스레드 1개는 이 수에 세지 않는다.
  - 근거: 약 6GB 파일에서 6이 가장 빨랐다. 그보다 많으면 오히려 조금 느려졌다.
  - 바꾸려면 `modern/SSAMDLL/include/SSAM.h`의 `DefaultNThreads()`와 `Source.cpp` 도움말의 숫자를 같이 고친다.
- 명령줄 `nthreads=`, `ttc=`, `pet=`가 한 글자 값(예: `ttc=1`, `pet=5`, `nthreads=8`)을 받는다.
  - 주의: 예전에는 한 글자 값이 무시되고 기본값(TTC 1.5, PET 5.0)으로 분석되었다. 예전에 `ttc=1`처럼 쓴 스크립트는 이제 결과가 달라진다.
- PET 0 이하와 스레드 수 0 이하는 오류로 거절한다.
- 읽기 오류가 나면 열린 TRJ 파일과 TRJ 텍스트 파일을 닫는다.

### 환경변수

| 이름 | 값 | 뜻 |
|---|---|---|
| `SSAM_PROFILE` | 아무 값 | 분석이 끝나면 구간별 시간(`[profile]` 줄)을 출력한다 |
| `SSAM_VERIFY_DUMP` | 파일 경로 | 분석이 끝나면 상충별 P(UEA)·mTTC·mPET 값을 그 파일에 쓴다 (검증용) |
| `SSAM_READER_THREAD` | `0` | 읽기 스레드를 끄고 예전처럼 한 스레드에서 읽는다 |

### 검증 방법

- 시험 자료와 기준 결과는 `.local/` 아래에 둔다 (git 제외). 저장소에는 넣지 않는다.
  - 시험 TRJ 파일 경로는 `.local/testdata/test-files.json`에 적는다 (`small`, `medium`, `large`).
  - 옛 형식·잘린 시험 파일 만들기: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/make-engine-testdata.ps1`
  - 기준 결과는 수정 전 엔진으로 한 번만 만든다 (`verify-engine.ps1 -MakeBaseline`). 다시 만들지 않는다.
- 엔진과 명령줄 프로그램만 빌드: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-release-mt-x64.ps1 -EngineOnly`
- 빠른 검증 (큰 파일 제외): `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/verify-engine.ps1 -Quick`
- 전체 검증 (큰 파일 포함): `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/verify-engine.ps1`
- 종료 코드 0이면 합격이다. 한 줄이라도 `MISMATCH`이면 불합격이다.
- 스크립트는 Windows PowerShell 5.1(`powershell`)로 돌린다.

### 다음 할 일

1. **화면 프로그램(SSAM3M.exe) 다시 빌드.**
   - 이번 작업에서 화면 프로그램은 다시 빌드하지 못했다. 빌드 도구에 MFC 라이브러리가 없다.
   - Visual Studio Installer에서 "C++ MFC for latest v143 build tools (x86 & x64)"를 설치한다.
   - `scripts/build-release-mt-x64.ps1`를 실행한다. 엔진, 명령줄 프로그램, 화면 프로그램을 빌드하고 엔진 DLL을 `modern/SSAM3/WorkingDir/`로 복사한다.
   - 빌드 전까지 `WorkingDir`의 화면 프로그램과 엔진은 예전 것 그대로 둔다. 새 엔진 DLL만 따로 넣지 않는다 (헤더가 바뀌어 섞으면 안 된다).
   - 빌드 뒤 화면 프로그램으로 분석을 한 번 돌려, 명령줄 프로그램과 상충 건수가 같은지 확인한다.
   - 화면 프로그램에서만 닿는 경로(엔진의 PET 0 거절, 오류 뒤 다시 분석)도 이때 확인한다.
2. 빅엔디언 TRJ 파일 확인 (시험 파일이 없어 확인하지 못했다).
3. 다음 작업 후보: 화면 프로그램의 진행률 표시·취소, 결과 목록 채우기 속도, TRJ 파일 여러 개 동시 분석, 빅엔디언 변환 버그, 한 차량이 다른 차량 안에 완전히 들어간 경우를 놓치는 판정.

## 범위 제외

- 알고리즘 개선 (분석 결과가 바뀌는 변경)
- 파일 포맷 변경
- 설치 패키지 정리
