# OpenSSAM

[English](README.md) | 한국어

OpenSSAM은 마이크로시뮬레이션 궤적 데이터 기반 대리안전도 분석을 위한 아카이브형 SSAM 3.0 코드베이스를 공개 상태로 되살리고 유지보수하기 위한 프로젝트입니다.

이 저장소는 기존 소스, 실행 파일, 문서 자료를 보존하는 동시에, 현대적인 UI, 개선된 알고리즘, 더 빠른 분석 워크플로우로 이어질 수 있는 기반을 만드는 것을 목표로 합니다.

저장소는 의도적으로 두 영역으로 나뉘어 있습니다.

- `archive/` - 복원된 역사적 배포본을 그대로 보존하는 영역
- `modern/` - OpenSSAM UI 및 엔진 개발을 진행하는 활성 작업 영역

## 프로젝트 방향

- 역사적인 SSAM 3.0 코드베이스와 문서를 공개 저장소로 보존
- 레거시 데스크톱 UI를 더 유지보수하기 쉬운 인터페이스로 교체
- 핵심 분석 로직과 병렬 실행 경로를 개선해 성능 향상
- 빌드, 문서화, 확장이 더 쉬운 프로젝트로 정리

## 저장소 구성

- `archive/` - 복원된 SSAM 배포본 전체 보관 영역
- `archive/SSAM3` - 원본 MFC GUI 소스와 Visual Studio 솔루션
- `archive/SSAMDLL` - 원본 분석 엔진, DLL, CLI 소스, 헤더, 라이브러리
- `archive/SSAM3EXE`, `archive/SSAM3EXE_x86` - 레거시 GUI 실행 파일과 의존성
- `archive/SSAMEXE` - 레거시 커맨드라인 실행 파일
- `archive/SSAM3Setup`, `archive/SSAM3Setup_x86` - 레거시 설치 패키지
- `archive/Documentation` - 아카이브된 사용자 매뉴얼, API 문서, 데이터 사전, 파일 포맷 문서
- `modern/` - OpenSSAM 활성 개발 소스 영역
- `modern/SSAM3` - OpenSSAM UI 개선 작업용 GUI 소스 트리
- `modern/SSAMDLL` - OpenSSAM 알고리즘 및 성능 개선 작업용 엔진 소스 트리
- `modern/Documentation` - OpenSSAM 작업에서 참조할 문서 사본

## 빠른 시작

일반 사용자는 가능하면 GitHub Releases에 올라가는 `SSAM3EXE` 패키지 배포본을 내려받아 사용하는 것을 기준으로 합니다.

`archive/` 트리는 참고, 보존, 소스 비교를 위한 영역이며, 기본적인 사용자 다운로드 경로로 보지는 않습니다.

보존된 아카이브 안의 레거시 데스크톱 애플리케이션을 직접 실행하려면 다음 경로를 확인하면 됩니다.

- `archive/SSAM3EXE/`
- `archive/SSAM3EXE_x86/`

커맨드라인 실행 예시는 다음과 같습니다.

```powershell
cd .\archive\SSAMEXE
.\SSAM_MT.exe trjfiles="C:\data\input.trj" csvfile="C:\data\output.csv" TTC=1.5 PET=5.0
```

멀티스레드 빌드에서는 `nthreads=<N>` 옵션도 사용할 수 있습니다.

## 빌드 메모

- 플랫폼: Windows
- IDE: Visual Studio 2012
- GUI 의존성: OpenSceneGraph
- 레거시 실행 파일은 Visual C++ Redistributable for Visual Studio 2012가 필요할 수 있음
- 아카이브 GUI 솔루션: `archive/SSAM3/SSAMAPP.sln`
- 아카이브 엔진 / CLI 솔루션: `archive/SSAMDLL/SSAMDLL.sln`
- 활성 GUI 솔루션: `modern/SSAM3/SSAMAPP.sln`
- 활성 엔진 / CLI 솔루션: `modern/SSAMDLL/SSAMDLL.sln`

## 1.0 UI 개선 사항

초기 1.0 포터블 배포본에는 작지만 실제 사용에 도움이 되는 UI 개선을 우선 반영했습니다.

- 메인 창 리사이즈 대응
- 창 크기 변경 시 탭 영역과 하단 버튼 재배치
- `Conflicts`, `Summary` 리스트 영역 크기 반영 및 컬럼 폭 정리
- `Map` 뷰어 영역 배치 안정화

아직은 레거시 UI 위에 적용한 1차 개선 단계이며, 더 넓은 범위의 UI 재설계는 장기 로드맵에 포함되어 있습니다.

## 로드맵

- 재현 가능한 빌드 절차 검증 및 문서화
- 필요 시 보관용 아티팩트와 적극 유지보수 대상 소스의 분리
- UI 재설계
- conflict detection 및 motion prediction 경로 프로파일링과 최적화
- 대용량 입력과 멀티스레드 분석 성능 개선
- 샘플 데이터, 테스트, 현대적인 개발자 문서 추가

## 아카이브 메모

이 프로젝트는 원래의 OSADP/SSAM 저장소에 더 이상 접근할 수 없게 되면서 시작되었습니다.

`archive/` 트리는 과거 공개되었던 SSAM 3.0 배포본의 복원 사본을 기반으로 유지됩니다. 코드와 문서가 계속 접근 가능하도록 보존하면서, 실제 OpenSSAM 개발 작업은 `modern/`에서 진행하는 구조입니다.

## 라이선스

이 저장소의 핵심 소스 파일은 `AGPL-3.0-or-later`로 제공됩니다. 자세한 내용은 루트 `LICENSE` 파일을 확인하면 됩니다. 과거 배포본에 포함된 서드파티 바이너리, 폰트, 설치 파일, 번들 의존성에는 별도 라이선스가 적용될 수 있습니다.

## Maintained By

OpenSSAM은 [S9 Systems](https://www.s9systems.kr/)가 비영리 오픈소스 활동으로 유지보수합니다.

- Website: https://www.s9systems.kr/
- Contact: shj@s9systems.kr

## Maintainer Note

이 프로젝트는 AI 보조를 적극적으로 활용해 개발하고 있습니다. 개선 제안이나 피드백을 주실 때는 가능하면 구현 이유, 변경 포인트, 적용 방법까지 자세히 설명해주시면 큰 도움이 됩니다.

## Personal Note

래리에게, 사랑을 담아.

## 기여

다음과 같은 기여를 환영합니다.

- 빌드 노트와 환경 수정
- 문서 정리
- 의존성 출처 및 라이선스 검토
- 성능 프로파일링과 최적화
- UI 현대화
