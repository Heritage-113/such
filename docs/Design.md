# Such — UI Design Philosophy

**Document:** `Design.md`  
**Product:** Such  
**Company:** Heritage Inc.  
**Date:** 2026-09-18  

---

## 0. 한 줄 정의

> **검색 전에는 검색창밖에 없다. 검색 후에는 필요한 파일부터 보이고, 나머지는 같은 리스트에서 계속 이어진다. 관리 기능은 제스처 뒤에 숨긴다.**

Such의 UI는 기능을 보여주기 위한 UI가 아니다.  
사용자가 **파일을 찾는 데 필요한 것만 남기고 나머지는 전부 뒤로 숨기는 UI**다.

엔진이 복잡할수록 화면은 더 단순해야 한다.

Such의 내부에서는 indexing, posting lookup, permission filtering, ranking, native icon resolution, extractor, external integrations, SDK가 돌아갈 수 있다.  
하지만 사용자가 보는 화면은 거의 다음뿐이어야 한다.

```text
┌──────────────────────────────────────────┐
│ 🔍 2026 Heritage Inc.                    │
└──────────────────────────────────────────┘

[OS icon][PDF]  project_report.pdf       📌
                X:\Project\...

[OS icon][DWG]  hospital_plan.dwg
                X:\Architecture\...

[OS icon][CPP]  SearchRuntime.cpp
                X:\Such\src\...

```

**복잡성은 엔진이 가져간다. 화면은 가져가지 않는다.**

---

# 1. Core Principles

## 1.1 Search is the interface

Such에서 검색창은 기능 중 하나가 아니다.

**검색창 자체가 앱이다.**

화면 최상단에는 검색창만 둔다.

검색창 위에 다음을 올리지 않는다.

- 로고
- `Such` 워드마크
- Heritage 로고
- toolbar
- indexing 상태
- 설정 버튼
- menu bar처럼 보이는 별도 header
- 의미 없는 설명문

브랜드는 검색을 방해하지 않는 곳에만 존재한다.

기본 화면에서 브랜드 표기는 별도 footer로 두지 않는다.

검색어가 비어 있을 때 검색창 내부에 매우 연한 회색 placeholder로 다음 문구만 표시한다.

```text
2026 Heritage Inc.
```

첫 글자를 입력하면 placeholder는 사라지고, query를 전부 지우면 다시 나타난다.

---

## 1.2 Compact first view, complete result list

Such는 첫 화면을 검색 결과로 가득 채우는 프로그램이 아니다.

**가장 필요한 결과를 먼저 보여주되, 필요한 나머지 결과도 바로 이어서 접근할 수 있어야 한다.**

기본 창 높이에서는 대략 5개 전후의 결과가 자연스럽게 보이는 compact viewport를 지향한다. 그러나 **5개는 표시 상한이 아니다.** 검색 결과가 더 있으면 같은 결과 리스트가 아래로 계속 이어지고, wheel/trackpad/touch/scrollbar로 전부 탐색한다.

```text
query
  ↓
candidate reduction
  ↓
permission filtering
  ↓
ranking
  ↓
ranked result stream
  ↓
compact viewport + scroll
```

결과가 수천 개 존재하더라도 첫 화면에 수십 개를 억지로 압축해 넣지 않는다. 대신 row의 읽기성과 랭킹 품질을 유지하면서 필요한 만큼 스크롤한다.

**첫 화면은 작게. 결과 집합은 잘라내지 않는다.**

---

## 1.3 Fast because it does less

Such의 디자인과 엔진은 같은 철학을 공유한다.

> **Fast because it does less.**

UI도 마찬가지다.

- 불필요한 panel을 만들지 않는다.
- 항상 보이는 설정 UI를 만들지 않는다.
- 큰 toolbar를 만들지 않는다.
- 별도 파일 탐색기처럼 변하지 않는다.
- 사용자가 검색 외의 구조를 학습하도록 요구하지 않는다.

작업에 필요할 때만 기능을 드러낸다.

---

# 2. Visual Language

## 2.1 Colors

기본 색상 체계는 종이와 진한 녹색이다.

### Paper / Off-white

```text
#F3EFE5
```

### Bright surface

```text
#FBF8EF
```

### Dark green

```text
#173A2B
```

Such는 순백색 SaaS dashboard처럼 보여서는 안 된다.

화면 전체는 약간 따뜻한 paper tone을 유지한다.

Dark green은 다음 요소에 사용한다.

- 검색창 outline
- active state
- extension badge
- swipe actions
- focus indication
- 강조 텍스트

---

## 2.2 Small curvature

Such는 둥글둥글한 모바일 카드 UI가 아니다.

곡률은 작게 유지한다.

권장 범위:

```text
Search field : 6–8 px
Result row   : 4–6 px
Button       : 4–6 px
Ext badge    : 3–4 px
```

금지:

```text
18 px
24 px
28 px
```

같은 과도한 radius를 기본 UI에 남발하는 것.

Such의 형태는 부드럽지만 **물렁하지 않다.**

---

## 2.3 Emboss, not decoration

Dark green outline과 paper surface 사이에는 가벼운 emboss/deboss 깊이감을 줄 수 있다.

단, 이 효과는 장식이 아니라 **경계 인지**를 위한 것이다.

허용:

- 얕은 inner highlight
- 얕은 shadow
- 눌린 듯한 search field edge
- 작은 focus ring

금지:

- 과도한 glassmorphism
- 배경 blur 남발
- 반짝이는 gradient
- SaaS hero card 스타일
- 불필요한 neon glow

---

## 2.4 Application Icon

Such의 canonical application icon은 **사용자가 제공한 원본 아트워크 그 자체**다. 임의로 다시 그리거나 한 글자 심볼로 축약하지 않는다.

시각적 식별 요소:

- warm cream / paper field
- dark green rounded outline
- 중앙의 소문자 `such` wordmark
- 원본에 없는 별도의 pictogram, gradient, glow를 추가하지 않는다.

권위 원본:

```text
assets/icon/SuchLogoOriginal.png
assets/icon/SuchLogoOriginal.ai
```

Windows taskbar/Start Menu, Linux desktop entry, iPadOS AppIcon은 모두 이 canonical artwork에서 비율을 보존해 파생한다.

---

# 3. Parametric Layout

Such UI는 좌표값을 여기저기 하드코딩해서 만들지 않는다.

UI는 **함수로 정의한다.**

```text
LayoutContext
    ↓
UI Scale
    ↓
LayoutMetrics
    ↓
SearchBar
ResultRow
SwipeActions
```

## 3.1 LayoutContext

예:

```cpp
struct LayoutContext {
    float width_px;
    float height_px;
    float dpi_scale;
    float content_scale;
};
```

## 3.2 LayoutMetrics

예:

```cpp
struct LayoutMetrics {
    float search_height;
    float row_height;

    float file_icon;
    float extension_badge;

    float filename_font;
    float path_font;

    float corner_radius;
    float horizontal_padding;

    float swipe_action_width;

    int visible_results;
};
```

## 3.3 Not everything scales equally

창이 20% 커졌다고 모든 요소를 20% 키우지 않는다.

강하게 반응해도 되는 값:

- horizontal padding
- content width
- search width
- result width

약하게 반응해야 하는 값:

- icon size
- font size
- badge
- corner radius

예:

```cpp
icon_size =
    clamp(base_icon * sqrt(scale),
          min_icon,
          max_icon);

radius =
    clamp(base_radius * sqrt(scale),
          4.0f,
          8.0f);
```

창이 커져도 아이콘과 곡률은 조금만 커져야 한다.

---

# 4. Search Field

## 4.1 Search field owns the top

검색창은 화면 최상단의 단독 요소다.

검색창보다 위에 다른 UI를 배치하지 않는다.

검색창은 다음을 만족한다.

- keyboard focus가 명확하다.
- DPI에 따라 자연스럽게 크기가 조절된다.
- 한글 IME가 정상 동작한다.
- 긴 query가 깨지지 않는다.
- `/pin` 같은 command query를 처리한다.
- 검색 결과가 실제 runtime에 연결된다.

검색어가 비어 있을 때 placeholder는 매우 연한 회색의 `2026 Heritage Inc.`다.

첫 글자가 들어오면 사라지고, query를 전부 지우면 다시 나타난다. 별도 footer는 두지 않는다.

placeholder는 브랜드 표기일 뿐이며 **검색 기능을 대신해서는 안 된다.**

---

# 5. Result Row

한 결과 행은 최대한 단순하다.

```text
[Native Icon + Ext Badge]  Filename                 📌
                           Path
```

구성:

1. OS native file icon
2. extension badge
3. filename
4. optional pin mark
5. path

## 5.1 Filename

filename이 가장 중요한 정보다.

- path보다 큰 font
- 한 줄
- width 부족 시 ellipsis
- row 전체 폭을 깨지 않는다.

사용 가능한 filename 폭은 계산한다.

```text
window width
- outer padding
- icon width
- icon gap
- optional pin mark
- right padding
```

파일명이 길다고 UI 전체를 축소하지 않는다.

## 5.2 Path

path는 secondary information이다.

- filename보다 작은 font
- 낮은 contrast
- 한 줄
- 필요 시 ellipsis

path는 검색 결과를 이해하는 데 충분해야 하지만 filename보다 시각적으로 강하면 안 된다.

---

# 6. Native File Icons

## 6.1 Such does not invent file icons

Such는 파일 아이콘을 직접 디자인하지 않는다.

금지:

- 자체 PDF icon
- 자체 DWG icon
- 자체 TXT icon
- 자체 CPP icon
- 확장자별 custom SVG/PNG icon set

운영체제가 파일 또는 파일 형식에 배정한 아이콘을 그대로 사용한다.

## 6.2 Platform policy

### Windows

우선순위:

1. 실제 파일에 대한 system icon / thumbnail
2. Shell file association icon
3. generic system document icon

가능한 API:

- `SHGetFileInfoW`
- `IShellItemImageFactory`
- Shell association APIs

### macOS (unsupported)

Desktop macOS support is intentionally excluded from Such v1.0.0 and later maintenance. No macOS build, bundle, runtime, or release path is maintained.

### Linux

- MIME type
- desktop icon theme
- file manager conventions

### iOS

- Files
- Document Provider
- platform-provided document representation

## 6.3 Extension badge

OS icon의 오른쪽 아래에 extension badge를 겹친다.

예:

```text
[ OS icon ]
       [PDF]
```

badge:

- dark green
- white border 약 2px
- 작은 radius
- 실제 extension 표시
- OS icon보다 강하게 보이지 않음

OS icon이 파일 형식을 표현하고, badge는 빠른 문자 인식을 보조한다.

---

# 7. Pin

Pinned 파일에는 filename 옆에 다음을 표시한다.

```text
📌
```

Pin은 단순 decoration이 아니다.

실제 persistent state와 연결한다.

검색창:

```text
/pin
```

→ pinned files only

```text
/pin report
```

→ pinned files 중 `report` 검색

Pin 상태 변경은 즉시 검색 결과에 반영돼야 한다.

---

# 8. Swipe Interaction

## 8.1 Swipe must be real

Such의 swipe는 버튼을 숨겨놓고 click으로 여는 UI가 아니다.

**row 자체가 pointer를 따라 움직여야 한다.**

왼쪽 drag:

```text
┌──────────────────────────┬──────────┬───────┬─────┐
│ file row                 │ Location │ Index │ Pin │
└──────────────────────────┴──────────┴───────┴─────┘
```

actions:

```text
Open location
Index
Pin / Unpin
```

## 8.2 Gesture state

권장 상태:

```cpp
struct SwipeState {
    int row;

    float start_x;
    float start_y;

    float initial_offset_x;
    float current_offset_x;

    float velocity_x;

    bool dragging;
    bool horizontal_locked;

    uint32_t pointer_id;
};
```

## 8.3 Gesture sequence

```text
Pointer down
    ↓
dead zone
    ↓
horizontal / vertical intent 판단
    ↓
horizontal lock
    ↓
pointer capture
    ↓
MOVE마다 row offset 갱신
    ↓
실시간 repaint
    ↓
release
    ↓
threshold + velocity 판단
    ↓
snap open / snap close
```

offset:

```text
-reveal_width <= offset_x <= 0
```

## 8.4 Input paths

### Mouse

```text
WM_LBUTTONDOWN
WM_MOUSEMOVE
WM_LBUTTONUP
```

### Touch / Pen

```text
WM_POINTERDOWN
WM_POINTERUPDATE
WM_POINTERUP
WM_POINTERCAPTURECHANGED
```

가능하면 precision touchpad의 horizontal input도 자연스럽게 연결한다.

## 8.5 Swipe behavior rules

- 왼쪽 swipe → actions reveal
- 오른쪽 swipe → close
- 다른 row를 열면 기존 row close
- 빈 공간 클릭 → close
- Escape → close
- focus loss → 합리적인 close/cancel
- vertical intent가 강하면 swipe cancel
- release threshold 미달이면 snap back
- 빠른 flick은 velocity로 open 가능

`open/closed` bool 하나만으로 그리지 않는다.

drag 중 intermediate offset을 실제 rendering에 사용한다.

---

# 9. Swipe Actions

## 9.1 Open location

### Windows

Explorer에서 **정확한 파일을 선택한 상태**로 reveal하는 것을 우선한다.

단순히 containing folder만 여는 것으로 끝내지 않는다.

### macOS (unsupported)

Desktop macOS support is intentionally excluded from Such v1.0.0 and later maintenance. No macOS build, bundle, runtime, or release path is maintained.

### Linux

가능하면 file manager select.

지원하지 않으면 containing directory open.

### iOS

Files / Document Provider가 허용하는 범위에서 해당 위치로 이동.

## 9.2 Index

Index action은 실제 index state와 연결한다.

버튼만 바뀌고 backend는 그대로인 placeholder implementation은 금지한다.

UI 상태 변경과 backend 상태 변경은 동기화되어야 한다.

## 9.3 Pin

Pin/Unpin은 persistent state를 바꾸고 `/pin` query에도 바로 반영한다.

---

# 10. Hidden Management

Such의 관리 기능은 존재해도 된다.

하지만 메인 화면에 상주하지 않는다.

예:

- indexed folders
- excluded folders
- file type coverage
- indexing status
- indexing priority
- quiet indexing
- advanced settings

이런 기능은:

- settings
- gesture
- secondary panel
- context menu

뒤에 숨긴다.

> **검색 전에는 검색창밖에 없다.**

이 원칙보다 관리 편의를 우선하지 않는다.

---

# 11. Window Behavior

## 11.1 Parametric window

창 크기는 유동적이어야 한다.

작아져도 UI가 망가지지 않고, 커져도 요소가 과하게 팽창하지 않는다.

기본 창은 대략 5개 전후의 결과를 안정적으로 읽을 수 있는 compact window를 지향한다. 결과가 더 많으면 row 높이를 억지로 줄이지 않고 결과 영역만 스크롤한다.

## 11.2 Taskbar

Windows에서 Such는 정상적인 앱처럼 작업표시줄에 나타나야 한다.

- `WS_EX_TOOLWINDOW`로 숨기지 않는다.
- 정상적인 taskbar presence를 유지한다.
- 사용자가 OS 방식으로 pin할 수 있게 한다.
- 비공식 shell hack으로 강제 pin하지 않는다.

---

# 12. Native First

Such는 native application이다.

금지:

- Electron
- WebView UI 대체
- HTML을 production GUI로 사용
- browser-style layout system에 제품 전체를 종속

플랫폼별 shell은 각 OS의 native behavior를 존중한다.

공통 C++ core 위에 thin native shell을 둔다.

```text
C++ Core
   ↓
UI state / layout / runtime client
   ↓
Platform native shell
```

---

# 13. Search and UI Boundary

UI는 검색엔진 내부 구현을 몰라도 된다.

이 경계를 유지한다.

```text
SearchRuntimeClient
        ↓
SearchResult
        ↓
Such UI
```

UI는 다음만 받아도 충분하다.

- file id
- filename
- path
- extension
- pin state
- index state
- icon request metadata
- authorization result

private storage backend 내부 구조나 posting representation이 UI에 새어 나오면 안 된다.

---

# 14. Production Search Policy

Production GUI에서 mock results를 보여주지 않는다.

검색 흐름:

```text
query
  ↓
tokenize
  ↓
posting lookup
  ↓
candidate file ids
  ↓
permission check
  ↓
ranking
  ↓
ranked result stream
  ↓
filename/path materialization
  ↓
scrollable result viewport
```

검색창 입력마다 전체 filesystem scan을 하지 않는다.

Foreground search와 background indexing은 분리한다.

로컬 standalone runtime에서는 `/index`(Windows) 또는 `//index`(Linux/iPadOS shell syntax)를 숨은 관리 진입점으로 사용한다. 재귀 filesystem crawl은 반드시 background worker에서 수행하고, query 입력마다 다시 crawl하지 않는다. `/reindex`는 기존 root 집합을 background에서 재생성하고 `/roots`는 현재 index root를 확인하는 관리 명령이다.

---

# 15. Permission-aware UI

권한은 UI decoration이 아니다.

권한 없는 파일은 **이름과 경로를 꺼내기 전에 제거**한다.

```text
candidate id
    ↓
permission gate
    ↓
authorized id
    ↓
filename/path materialization
    ↓
UI
```

금지:

```text
filename/path materialize
    ↓
UI에서 숨김
```

UI, SDK, external integrations, Open location은 같은 permission authority를 따라야 한다.

---

# 16. Performance Philosophy

Such의 UI thread는 검색엔진이 아니다.

UI thread에서 하지 말 것:

- recursive filesystem crawl
- heavy indexing
- large file parsing
- blocking extractor execution
- repeated disk access during paint

가능하면:

- native icon cache
- dirty-row invalidation
- query generation / cancellation
- background indexing
- async icon resolution
- GDI object reuse
- resize 시 불필요한 font recreation 방지

를 사용한다.

---

# 17. Accessibility and Fallback

Swipe는 빠른 interaction이지만 유일한 interaction이면 안 된다.

Mouse/keyboard 사용자는 다음 중 하나로 같은 actions에 접근할 수 있어야 한다.

- `...`
- context menu
- keyboard context action

기본 keyboard behavior:

```text
Search focus
Up / Down → result 이동
Enter     → open
Esc       → swipe/context close
```

UI를 복잡하게 만들지 않는 범위에서 접근성을 확보한다.

---

# 18. Empty State

검색 결과가 없다고 큰 illustration이나 onboarding card를 띄우지 않는다.

작고 조용하게 처리한다.

예:

```text
No matches
```

또는 결과 영역 자체를 compact하게 축소한다.

Such는 검색 실패조차 과장하지 않는다.

---

# 19. What Such Must Never Become

Such는 다음으로 변하면 안 된다.

### 파일 탐색기

폴더 트리, 드라이브 트리, 큰 preview pane이 기본 화면을 점령하면 안 된다.

### SaaS dashboard

카드, 통계, onboarding panel, account widget으로 화면을 채우지 않는다.

### Toolbar application

수십 개 버튼을 상단에 나열하지 않는다.

### Custom icon theme

OS의 파일 의미 체계를 다시 디자인하지 않는다.

### Search benchmark toy

검색 latency 숫자만 빠르고 사용자가 원하는 결과를 못 찾으면 실패다.

---

# 20. Design Contract

Such UI를 수정할 때 아래 항목을 항상 확인한다.

```text
[ ] Search field가 최상단 단독 요소인가?
[ ] 기본 viewport는 compact한가, 그리고 5개를 넘는 결과도 스크롤로 접근 가능한가?
[ ] 곡률이 작은가?
[ ] Paper + dark green 체계가 유지되는가?
[ ] OS native file icon을 쓰는가?
[ ] Extension badge가 overlay되는가?
[ ] Pin 표시가 실제 state와 연결되는가?
[ ] /pin이 실제 query인가?
[ ] Swipe가 MOVE 중 실제로 따라오는가?
[ ] Location / Index / Pin이 실제 callback과 연결되는가?
[ ] Layout이 parametric한가?
[ ] DPI에 안전한가?
[ ] filename이 긴 경우 ellipsis가 되는가?
[ ] management UI가 메인 화면을 침범하지 않는가?
[ ] production UI에 mock result가 남아있지 않은가?
[ ] permission filtering이 materialization 이전에 일어나는가?
[ ] UI thread에서 heavy indexing을 하지 않는가?
[ ] Windows taskbar behavior가 정상인가?
[ ] 검색어가 비어 있을 때 검색창 placeholder가 연한 `2026 Heritage Inc.`인가?
[ ] 별도 footer가 메인 화면을 차지하지 않는가?
```

하나라도 깨졌다면 Such의 UI 철학과 충돌하는지 검토한다.

---

# 21. Final Principle

Such의 디자인은 기능을 과시하지 않는다.

사용자가 원하는 건 검색엔진의 내부 구조를 보는 것이 아니다.

원하는 파일을 찾는 것이다.

따라서 Such의 화면은 끝까지 다음 원칙을 유지한다.

> **겉은 단순하게. 뒤는 미친 듯이 빠르게.**

그리고 그 단순함은 기능 부족이 아니라 **복잡성을 뒤로 밀어낸 결과**여야 한다.

---

**2026 Heritage Inc.**
