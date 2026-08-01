# EnemyCannon 개발 계획

## 0. 문서 목적

이 문서는 함선 갑판의 적 캐릭터가 고정식 대포를 검색하고 예약한 뒤, 대포에 탑승하여 이동을 멈추고 Player를 조준·공격하는 기능의 구현 계획을 정의한다.

다른 작업 채팅에서도 이 문서만 읽고 구현을 이어갈 수 있도록 다음 내용을 포함한다.

- 확정된 설계 결정과 범위
- 기존 코드에서 재사용할 부분
- 새로 만들 파일과 수정할 파일
- 마일스톤별 구현 순서
- 각 마일스톤의 완료 조건
- 네트워크, 충돌, GAS 및 실패 복구 정책
- 자동화 테스트와 PIE 테스트 항목

이 문서에서 말하는 `EnemyCannon`은 다음 클래스와 파일을 뜻한다.

```text
Source/Enemy/Public/Cannon/EnemyCannon.h
Source/Enemy/Private/Cannon/EnemyCannon.cpp
```

---

## 1. 최종 목표

다음 전투 루프를 구현한다.

```text
Player 감지
-> 같은 함선의 사용 가능한 EnemyCannon 검색
-> Player 위치와 대포 조준 범위를 기준으로 후보 점수 계산
-> 점수순으로 예약 시도
-> 예약한 대포의 EnemyMountPoint까지 이동
-> Enemy를 대포에 Attach
-> Enemy CharacterMovement 정지
-> AIController와 Behavior Tree는 Enemy를 계속 제어
-> 대포만 회전시켜 Player를 조준
-> 기존 Cannon 발사 파이프라인으로 포탄 발사
-> 쿨다운 후 재조준 및 반복 발사
-> 표적 상실, 사망, 대포 파괴 또는 BT Abort 시 하차 및 예약 해제
-> 기존 총/칼 전투로 복귀
```

Player도 `AEnemyCannon`을 사용할 수 있어야 한다.

```text
Player 상호작용
-> AEnemyCannon::Board()
-> 기존 ACannon Player Possess/Input/UI 경로 사용
-> Player가 직접 조준 및 발사
-> Exit 시 Player Character로 Possess 복원
```

Player 조작과 Enemy 조작은 상호 배타적이다.

---

## 2. 확정된 설계 결정

### 2.1 휴대 무기는 기존 시스템을 유지한다

다음 무기는 예약 시스템의 대상이 아니다.

- 칼
- 총
- 활
- Enemy가 직접 들고 이동하는 기타 휴대 무기

휴대 무기는 기존 시스템을 그대로 사용한다.

```text
UBaseWeaponComponent
-> UWeaponDataAsset
-> FWeaponDefinition
-> GrantedAbilities
-> UGA_BasicAttack 또는 무기별 Ability
-> UBTT_EnemyBasicAttack
```

Enemy가 대포를 사용할 때도 대포를 `UBaseWeaponComponent::CurrentWeapon`으로 설정하지 않는다. 기존 휴대 무기는 대포 탑승 중 Holster하고, 하차 후 다시 Equip한다.

### 2.2 EnemyCannon은 ACannon을 상속한다

```cpp
UCLASS()
class ENEMY_API AEnemyCannon : public ACannon
{
    GENERATED_BODY()
};
```

상속을 통해 다음 기능을 재사용한다.

- 대포 Mesh와 Barrel 회전
- Player용 Camera
- Player Input Mapping
- Player Board/Exit 흐름
- `FireCannon()`
- `SetAIAimRotation()`
- 포탄 Spawn
- 대포 쿨다운
- 함선 GAS의 CannonDamage, CannonballSpeed, CannonFireCooldown
- Water Bomb 등 기존 Player Cannon 기능

Enemy 전용 예약, Attach, AI 조준, 강제 하차는 `AEnemyCannon`에만 구현한다.

### 2.3 Player는 Cannon을 Possess하고 Enemy는 Possess하지 않는다

Player 조작:

```text
PlayerController -> AEnemyCannon Possess
```

Enemy 조작:

```text
ABaseAIController -> ABaseEnemy Possess 유지
ABaseEnemy -> EnemyCannon의 EnemyMountPoint에 Attach
```

Enemy AIController가 Cannon을 Possess하게 만들지 않는다. 그래야 Blackboard, Behavior Tree, TargetActor와 Enemy GAS 상태가 유지된다.

### 2.4 Enemy는 대포 탑승 후 이동할 수 없다

Enemy가 대포에 탑승하면 다음 상태가 된다.

- `CharacterMovement` 중지 및 비활성화
- 현재 Move 요청 중단
- 대포의 `EnemyMountPoint`에 Snap Attach
- 대포 Yaw 회전에 따라 Enemy도 함께 회전
- 기존 휴대 무기 Holster
- `State.Operating.Cannon` GameplayTag 추가
- Player 공격 Trace에 맞을 수 있도록 Query Collision 유지

Enemy의 Actor Collision을 완전히 끄지 않는다. 완전히 끄면 Player의 총/칼 Trace가 탑승 중인 Enemy를 맞히지 못할 수 있다.

### 2.5 대포가 예약 상태를 소유한다

`AEnemyCannon`은 다음 두 상태를 구분한다.

```text
ReservedEnemy: 대포로 이동 중인 Enemy
MountedEnemy: 대포에 Attach되어 실제 조작 중인 Enemy
```

예약 확인과 설정은 서버의 `TryReserveForEnemy()` 한 함수에서 처리한다.

### 2.6 발사 팀은 함선보다 실제 조작자를 우선한다

Enemy가 Player 함선의 대포를 사용할 수도 있으므로 포탄 팀을 MountShip만으로 결정하면 안 된다.

팀 판정 우선순위:

```text
1. MountedEnemy
2. RidingPlayer
3. 기존 적 함선 AI와 같은 Operator 없는 호출은 MountShip
4. 모두 없으면 중립
```

### 2.7 대포를 사용할 수 없으면 기존 무기 전투로 돌아간다

다음 경우 Enemy는 멈추지 않고 기존 총/칼 전투를 수행한다.

- 같은 배에 EnemyCannon이 없음
- 모든 EnemyCannon이 예약 또는 사용 중
- 모든 대포에서 탄도해가 없음
- 대포까지 이동할 수 없음
- 대포가 파괴 또는 비활성화됨
- 대포 조작이 허용되지 않은 Enemy 유형

---

## 3. 모듈 의존 방향

현재 `Enemy` 모듈은 `WaterAndShip` 모듈에 의존한다.

```text
Enemy -> WaterAndShip
```

따라서 `AEnemyCannon : ACannon`은 `Enemy` 모듈에 구현할 수 있다.

다음 역방향 의존은 만들지 않는다.

```text
WaterAndShip -> Enemy
```

`ACannon`은 `ABaseEnemy`를 알지 않는다. Base 확장이 필요하면 `APawn`, virtual hook 또는 일반적인 발사 Context만 사용한다.

---

## 4. 주요 클래스 책임

### 4.1 `ACannon`

위치:

```text
Source/WaterAndShip/Public/Cannon.h
Source/WaterAndShip/Private/Cannon.cpp
```

책임:

- Player Board 및 Exit
- PlayerController Possess
- Enhanced Input
- Aim UI
- Cannon Mesh 회전
- 발사 쿨다운
- 함선 Attribute 조회
- 포탄 Spawn
- 기존 Player Cannon 기능

이번 작업에서는 Enemy 전용 상태를 넣지 않는다. 자식 클래스가 확장할 수 있도록 최소한의 virtual hook만 추가한다.

### 4.2 `AEnemyCannon`

위치:

```text
Source/Enemy/Public/Cannon/EnemyCannon.h
Source/Enemy/Private/Cannon/EnemyCannon.cpp
```

책임:

- Enemy 예약과 예약 토큰 발행
- Player와 Enemy 사용 상호 배제
- EnemyMountPoint 및 EnemyDismountPoint 제공
- Enemy Attach/Dismount
- 탑승 중 Enemy 상태 복제
- Player 위치에 대한 대포 탄도해 계산
- AI 조준각 보간
- 탑승 Enemy의 발사 요청 검증
- Enemy 사망, 대포 파괴, Lease 만료 시 정리

### 4.3 `UEnemyCannonOperatorComponent`

권장 위치:

```text
Source/Enemy/Public/Cannon/EnemyCannonOperatorComponent.h
Source/Enemy/Private/Cannon/EnemyCannonOperatorComponent.cpp
```

책임:

- Enemy가 예약한 Cannon과 ReservationId 보관
- 현재 탑승 Cannon 보관
- Enemy 원래 이동/충돌/복제 상태 Snapshot 보관
- 예약, 탑승, 하차 요청의 단일 진입점
- BT Abort, Enemy EndPlay, Enemy 사망 시 예약 해제
- 휴대 무기 Holster/Equip

이 Component는 Tick하지 않는다.

### 4.4 Behavior Tree 노드

권장 위치:

```text
Source/Enemy/Public/Task/BTT_FindAndReserveEnemyCannon.h
Source/Enemy/Private/Task/BTT_FindAndReserveEnemyCannon.cpp

Source/Enemy/Public/Task/BTT_MoveToReservedEnemyCannon.h
Source/Enemy/Private/Task/BTT_MoveToReservedEnemyCannon.cpp

Source/Enemy/Public/Task/BTT_MountReservedEnemyCannon.h
Source/Enemy/Private/Task/BTT_MountReservedEnemyCannon.cpp

Source/Enemy/Public/Task/BTT_OperateEnemyCannon.h
Source/Enemy/Private/Task/BTT_OperateEnemyCannon.cpp

Source/Enemy/Public/Task/BTT_DismountEnemyCannon.h
Source/Enemy/Private/Task/BTT_DismountEnemyCannon.cpp

Source/Enemy/Public/Decorator/BTD_HasValidEnemyCannonReservation.h
Source/Enemy/Private/Decorator/BTD_HasValidEnemyCannonReservation.cpp

Source/Enemy/Public/Service/BTS_UpdateEnemyCannonContext.h
Source/Enemy/Private/Service/BTS_UpdateEnemyCannonContext.cpp
```

---

## 5. 대포 상태 모델

```cpp
UENUM(BlueprintType)
enum class EEnemyCannonOperationState : uint8
{
    Available,
    ReservedForEnemy,
    EnemyMounted,
    PlayerControlled,
    Disabled
};
```

상태 전이:

```text
Available
-> ReservedForEnemy: Enemy 예약 성공
-> PlayerControlled: Player Board 성공
-> Disabled: 대포 비활성화

ReservedForEnemy
-> EnemyMounted: 예약 Enemy가 도착하여 Mount 성공
-> Available: 예약 취소, Lease 만료, Enemy 사망
-> Disabled: 대포 파괴 또는 비활성화

EnemyMounted
-> Available: 하차 완료
-> Disabled: 대포 파괴 또는 비활성화

PlayerControlled
-> Available: Player Exit
-> Disabled: ForceExit 후 비활성화
```

불변 조건:

- `RidingPlayer`와 `MountedEnemy`는 동시에 유효할 수 없다.
- `ReservedEnemy`와 다른 `MountedEnemy`가 동시에 유효할 수 없다.
- `MountedEnemy`가 있으면 예약자는 같은 Enemy이거나 `ReservedEnemy`가 null이어야 한다.
- 예약과 탑승 상태 변경은 서버에서만 실행한다.

---

## 6. 개발 마일스톤

### 마일스톤 0: 기준선 확인과 테스트 준비

#### 목표

기존 Cannon 기능의 현재 동작을 기록하고, 이후 변경으로 발생하는 회귀를 찾을 수 있는 기준선을 만든다.

#### 확인할 기존 파일

```text
Source/WaterAndShip/Public/Cannon.h
Source/WaterAndShip/Private/Cannon.cpp
Source/WaterAndShip/Public/Cannonball.h
Source/WaterAndShip/Private/Cannonball.cpp
Source/Enemy/Public/ShipAI/EnemyShip.h
Source/Enemy/Private/ShipAI/EnemyShip.cpp
Source/ClassFeature/Private/BasePlayer.cpp
Source/Enemy/Public/BaseEnemy.h
Source/Enemy/Private/BaseEnemy.cpp
```

#### 개발 항목

- [ ] 현재 Player의 Cannon Board/Exit 흐름 확인
- [ ] `ABasePlayer`가 `ACannon*`으로 캐스팅한 뒤 `Board()`를 호출한다는 점 확인
- [ ] `ACannon::Board()`와 `FireCannon()`의 virtual 여부 확인
- [ ] `RidingPlayer`가 Base private 상태라는 점 확인
- [ ] 기존 EnemyShip AI가 `SetAIAimRotation()`과 `FireCannon()`을 사용하는 경로 확인
- [ ] Player Cannon 발사, Exit, 재탑승 수동 테스트
- [ ] EnemyShip 자동 대포 발사 수동 테스트
- [ ] Water Bomb Cannon 모드 수동 또는 기존 자동화 테스트 실행
- [ ] Dedicated Server 또는 Listen Server에서 Cannon 복제 동작 기록

#### 산출물

- 기존 테스트 결과
- 변경 전 정상 동작 로그 또는 간단한 체크리스트
- 변경 후 반드시 유지해야 하는 회귀 테스트 목록

#### 완료 조건

- Player Cannon, EnemyShip Cannon, Water Bomb의 기준 동작을 재현할 수 있다.
- 어떤 기존 API를 호환 유지해야 하는지 목록이 확정되었다.

---

### 마일스톤 1: ACannon 확장 지점 추가

#### 목표

`AEnemyCannon`이 Player 점유 정책과 발사자를 변경할 수 있도록 `ACannon`에 최소한의 virtual hook을 추가한다. Enemy 관련 타입이나 상태는 Base에 넣지 않는다.

#### 수정 파일

```text
Source/WaterAndShip/Public/Cannon.h
Source/WaterAndShip/Private/Cannon.cpp
```

#### 개발 항목

- [ ] `Board()`를 virtual 함수로 변경
- [ ] 가능하면 `Board()`가 성공 여부를 `bool`로 반환하도록 변경
- [ ] 기존 호출부와 테스트가 반환값을 무시해도 정상 컴파일되는지 확인
- [ ] `CanBoard(APawn*) const` virtual hook 추가
- [ ] `OnPlayerBoarded(APawn*)` virtual hook 추가
- [ ] `OnPlayerExited(APawn*)` virtual hook 추가
- [ ] `ResolveFiringOperator() const` virtual hook 추가
- [ ] `SpawnParams.Instigator`가 `ResolveFiringOperator()`를 사용하도록 변경
- [ ] Player Exit와 ForceExit에서 자식 hook이 누락되지 않도록 정리
- [ ] 기존 `ACannon` 기본 동작은 현재 구현과 동일하게 유지

#### 권장 API

```cpp
public:
    UFUNCTION(BlueprintCallable, Category = "Cannon")
    virtual bool Board(APawn* PlayerPawn);

protected:
    virtual bool CanBoard(APawn* PlayerPawn) const;
    virtual void OnPlayerBoarded(APawn* PlayerPawn);
    virtual void OnPlayerExited(APawn* PlayerPawn);
    virtual APawn* ResolveFiringOperator() const;
```

#### 구현 주의사항

- `WaterAndShip`에서 `ABaseEnemy`를 include하지 않는다.
- Base의 Player Input과 UI 코드를 자식 클래스에 복사하지 않는다.
- `AEnemyShip`의 기존 `FireCannon()` 호출이 계속 동작해야 한다.
- Player가 `ACannon*`을 통해 `Board()`를 호출해도 자식 override가 실행되어야 한다.
- `RidingPlayer`가 private이면 자식은 `GetRidingPlayer()`를 사용한다.

#### 자동화/기능 테스트

- [ ] 기존 `ACannon`에 Player가 Board 가능
- [ ] Player Exit 후 Character Possess 복원
- [ ] 연속 Board/Exit 가능
- [ ] EnemyShip AI 대포 발사 유지
- [ ] Water Bomb Ability 테스트 유지

#### 완료 조건

- `ACannon`의 기존 기능이 유지된다.
- 자식 클래스가 Board 허용 여부와 발사 Operator를 override할 수 있다.
- `WaterAndShip -> Enemy` 의존이 생기지 않는다.

---

### 마일스톤 2: AEnemyCannon 기본 클래스와 점유 상태

#### 목표

`AEnemyCannon` 클래스, 예약 상태, 탑승 지점과 Player/Enemy 상호 배제를 구현한다.

#### 새 파일

```text
Source/Enemy/Public/Cannon/EnemyCannon.h
Source/Enemy/Private/Cannon/EnemyCannon.cpp
```

#### 개발 항목

- [ ] `AEnemyCannon : ACannon` 선언
- [ ] `EnemyMountPoint` SceneComponent 생성
- [ ] `EnemyDismountPoint` SceneComponent 생성
- [ ] `EnemyMountPoint`를 대포 Yaw와 함께 회전하는 Component에 부착
- [ ] `EnemyDismountPoint`를 Cannon Root 기준으로 부착
- [ ] `ReservedEnemy` 복제 상태 추가
- [ ] `MountedEnemy` 복제 상태 추가
- [ ] `ReservationRevision` 추가
- [ ] `EEnemyCannonOperationState` 추가
- [ ] `GetOperationState()` 구현
- [ ] `TryReserveForEnemy()` 구현
- [ ] `ReleaseEnemyReservation()` 구현
- [ ] Player Board 가능 여부 override
- [ ] Player가 Board하면 Enemy 예약/탑승이 없음을 재검증
- [ ] Enemy가 예약하려면 Player가 조작 중이 아님을 재검증
- [ ] `EndPlay()`에서 예약과 탑승 상태 정리
- [ ] RepNotify와 `GetLifetimeReplicatedProps()` 구현

#### 권장 API

```cpp
bool TryReserveForEnemy(
    ABaseEnemy* Enemy,
    uint32& OutReservationId);

bool ReleaseEnemyReservation(
    ABaseEnemy* Enemy,
    uint32 ReservationId);

bool IsReservationValid(
    const ABaseEnemy* Enemy,
    uint32 ReservationId) const;

bool IsAvailableForEnemy(
    const ABaseEnemy* Enemy) const;

FTransform GetEnemyMountWorldTransform() const;
FTransform GetEnemyDismountWorldTransform() const;
```

#### 예약 규칙

- 서버에서만 성공
- null, PendingKill, 죽은 Enemy 거부
- Player 조작 중이면 거부
- 다른 Enemy 탑승 중이면 거부
- 다른 Enemy 예약 중이면 거부
- 같은 Enemy의 동일 예약 요청은 idempotent 성공
- 예약 변경마다 `ReservationRevision` 증가
- Release 시 Enemy와 Revision을 모두 검사

#### 에디터 작업

- [ ] `BP_EnemyCannon` 생성
- [ ] Parent Class를 `AEnemyCannon`으로 지정
- [ ] `EnemyMountPoint`를 포수 위치에 배치
- [ ] `EnemyDismountPoint`를 안전한 하차 위치에 배치
- [ ] 기존 Cannon Mesh, 포탄 Class, Input, UI 설정 상속 확인
- [ ] Player가 상호작용할 수 있도록 기존 Interactable 설정 확인

#### 자동화/기능 테스트

- [ ] 빈 EnemyCannon을 Enemy A가 예약할 수 있음
- [ ] Enemy A가 예약한 Cannon을 Enemy B가 예약할 수 없음
- [ ] 잘못된 ReservationId로 해제할 수 없음
- [ ] Enemy 예약 중 Player Board 실패
- [ ] Player 조작 중 Enemy 예약 실패
- [ ] 예약 해제 후 Player 또는 다른 Enemy가 사용 가능

#### 완료 조건

- 한 대포에 Player 또는 Enemy 한 명만 점유권을 가질 수 있다.
- Player가 `BP_EnemyCannon`을 기존 Cannon처럼 사용할 수 있다.
- 아직 Enemy Attach와 AI 발사는 하지 않아도 된다.

---

### 마일스톤 3: EnemyCannonOperatorComponent와 안전한 상태 보관

#### 목표

Enemy 쪽에서 예약과 탑승 수명주기를 관리하고, BT Abort와 Enemy 제거 시 대포 상태가 누수되지 않도록 한다.

#### 새 파일

```text
Source/Enemy/Public/Cannon/EnemyCannonOperatorComponent.h
Source/Enemy/Private/Cannon/EnemyCannonOperatorComponent.cpp
```

#### 수정 파일

```text
Source/Enemy/Public/BaseEnemy.h
Source/Enemy/Private/BaseEnemy.cpp
```

갑판 전용 Enemy subclass를 별도로 만들 경우 Component는 `ABaseEnemy`가 아니라 해당 subclass에 추가해도 된다. 구현 시작 전에 현재 Enemy 생성 경로를 확인하고 하나를 선택한다.

#### 개발 항목

- [x] Tick 없는 `UEnemyCannonOperatorComponent` 작성
- [x] 현재 예약 Cannon 저장
- [x] ReservationId 저장
- [x] 현재 Mounted Cannon 저장
- [x] Enemy 원래 상태 Snapshot 구조체 작성
- [x] `TryReserveCannon()` 구현
- [x] `MountReservedCannon()` 구현 진입점 작성
- [x] `DismountAndRelease()` 구현 진입점 작성
- [x] `HasValidReservation()` 구현
- [x] `RefreshLease()` 구현 준비
- [x] Enemy EndPlay에서 정리
- [x] Enemy HealthComponent 사망 Delegate에 연결
- [x] 중복 Release를 idempotent하게 처리

#### Snapshot에 저장할 상태

```text
CharacterMovement MovementMode
Capsule CollisionEnabled
Capsule Collision Profile 또는 필요한 Response
bReplicateMovement
기존 Attach Parent
기존 Attach Socket
기존 Weapon Equipped 상태
필요한 경우 AI Focus
```

#### 구현 주의사항

- 하차 시 `MOVE_Walking`으로 무조건 고정하지 말고 저장한 MovementMode를 복원한다.
- Collision Profile을 하드코딩하지 말고 원래 값을 복원한다.
- Enemy가 이미 파괴 중이면 대포 상태만 정리하고 Character 상태 복원을 강제하지 않는다.
- Component와 Cannon 양쪽의 정리 코드는 중복 호출되어도 안전해야 한다.

#### 자동화/기능 테스트

- [x] Component가 예약 성공 후 Cannon과 ReservationId를 보관
- [x] Enemy EndPlay 시 예약 해제
- [x] Enemy 사망 시 예약 해제
- [x] 대포 EndPlay 후 Component 참조가 무효 처리
- [x] 두 번 Release해도 오류나 다른 사용자의 예약 해제가 없음

#### 구현 상태와 범위

2026-07-31 기준 마일스톤 3 구현 파일:

```text
Source/Enemy/Public/Cannon/EnemyCannonOperatorComponent.h
Source/Enemy/Private/Cannon/EnemyCannonOperatorComponent.cpp
Source/Enemy/Private/Tests/EnemyCannonMilestone3Tests.cpp
Source/Enemy/Public/BaseEnemy.h
Source/Enemy/Private/BaseEnemy.cpp
```

- `ABaseEnemy` 생성자에서 `CannonOperatorComponent`를 기본 Subobject로 생성한다.
- 컴포넌트는 서버에서만 예약을 획득·해제하며 자체 Tick과 복제를 사용하지 않는다.
- 유효한 예약을 보유한 동안 다른 Cannon으로 묵시적으로 전환하지 않는다.
- Enemy 사망, Enemy EndPlay, Cannon EndPlay 경로에서 참조와 예약을 정리한다.
- `DismountAndRelease()`는 여러 번 호출해도 안전하며 오래된 Token으로 다른 Enemy의 예약을 해제하지 않는다.
- `RefreshLease()`는 현재 Heartbeat 시간만 기록한다. 실제 Lease 만료 판정은 마일스톤 9 범위다.
- `MountReservedCannon()`은 마일스톤 4에서 실제 Attach, 이동 정지 및 충돌 전환까지 구현되었다.

#### 간단한 테스트 루프

자동화 테스트:

```text
Unreal Editor
-> Window
-> Test Automation
-> 검색: ArtisticSW.EnemyCannon.Milestone3
-> ComponentReservationLifecycle 실행
-> DeathAndEndPlayCleanup 실행
```

Output Log 또는 명령행에서는 다음 필터를 사용할 수 있다.

```text
Automation RunTests ArtisticSW.EnemyCannon.Milestone3
```

테스트가 확인하는 반복 루프:

```text
Enemy Component -> Cannon A 예약
-> 같은 Cannon 재요청 시 같은 ReservationId 유지
-> Cannon B 전환 요청 거부
-> Release
-> 다시 Release해도 안전

Enemy Component -> Cannon 예약
-> Enemy 사망 또는 Enemy EndPlay
-> Cannon이 Available로 복귀

Enemy Component -> Cannon 예약
-> Cannon EndPlay
-> Component의 Cannon 참조와 ReservationId가 0/null로 정리
```

#### 에디터에서 해야 할 작업

1. C++ 빌드 후 Editor를 재시작하거나 새 모듈을 로드한다.
2. 기존 Enemy Blueprint를 열고 `Inherited Components`에 `CannonOperatorComponent`가 보이는지 확인한다.
3. 보이지 않으면 Blueprint를 `Compile`한 뒤 다시 연다. 마일스톤 3에서 별도 프로퍼티 설정은 필요 없다.
4. `BP_EnemyCannon`의 Parent Class가 `AEnemyCannon`인지 확인한다.
5. `BP_EnemyCannon`의 `EnemyMountPoint`와 `EnemyDismountPoint` 위치는 마일스톤 2 설정을 유지한다.
6. Test Automation에서 `ArtisticSW.EnemyCannon.Milestone3` 테스트 두 개를 실행한다.
7. 선택적 수동 확인은 Level Blueprint 또는 임시 Debug Blueprint에서 다음 노드로 수행한다.

```text
Enemy Reference
-> Get Cannon Operator Component
-> Try Reserve Cannon(BP_EnemyCannon Reference)
-> EnemyCannon Get Operation State == ReservedForEnemy 확인
-> Dismount And Release
-> EnemyCannon Get Operation State == Available 확인
```

8. `Mount Reserved Cannon`은 마일스톤 4부터 동일 함선·거리 조건을 만족하면 실제 탑승에 성공한다.

#### 완료 조건

- Enemy 또는 Cannon이 제거되어도 예약이 영구적으로 남지 않는다.
- 이후 BT Task는 Component API만 사용해 예약과 하차를 제어할 수 있다.

---

### 마일스톤 4: Enemy Attach, 이동 정지 및 하차

#### 목표

예약 Enemy를 Cannon MountPoint에 실제로 부착하고, 이동을 막은 상태에서 피격 가능성을 유지한다. 하차하면 모든 상태를 원래대로 복원한다.

#### 수정 파일

```text
Source/Enemy/Public/Cannon/EnemyCannon.h
Source/Enemy/Private/Cannon/EnemyCannon.cpp
Source/Enemy/Public/Cannon/EnemyCannonOperatorComponent.h
Source/Enemy/Private/Cannon/EnemyCannonOperatorComponent.cpp
Source/Enemy/Private/Tests/EnemyCannonMilestone4Tests.cpp
Source/ArtisticSWCore/Public/BaseGameplayTags.h
Source/ArtisticSWCore/Private/BaseGameplayTags.cpp
Source/WaterAndShip/Public/Cannon.h
Source/WaterAndShip/Private/Cannon.cpp
```

#### 탑승 전 검증

- [x] 서버 권한
- [x] Enemy 생존
- [x] ReservationId 일치
- [x] `ReservedEnemy == Enemy`
- [x] `MountedEnemy == nullptr`
- [x] `GetRidingPlayer() == nullptr`
- [x] Enemy와 Cannon이 같은 HostShip
- [x] Enemy가 MountPoint 허용 반경 안
- [x] Cannon이 Disabled가 아님

#### 탑승 순서

- [x] AIController의 현재 Move 요청 중단
- [x] CharacterMovement `StopMovementImmediately()`
- [x] 기존 휴대 무기 `UnequipCurrentWeapon()`
- [x] Enemy 원래 상태 Snapshot 저장
- [x] CharacterMovement 비활성화
- [x] Capsule Collision을 QueryOnly로 변경
- [x] 물리 충돌은 막되 Pawn/Weapon/Visibility Query 피격 유지
- [x] 필요하면 `SetReplicateMovement(false)`
- [x] `EnemyMountPoint`에 Snap Attach
- [x] 상대 위치와 회전이 정확한지 확인
- [x] `MountedEnemy` 설정
- [x] `ReservedEnemy` 정리 또는 같은 Enemy로 유지하는 정책 확정
- [x] `State.Operating.Cannon` GameplayTag 추가

#### 하차 순서

- [ ] 발사 루프 중단
- [ ] Cannon 조준을 중립 위치로 복귀
- [x] `State.Operating.Cannon` 제거
- [x] Enemy Detach
- [x] `EnemyDismountPoint`를 갑판 안전 위치로 투영
- [x] 안전한 Dismount Transform으로 이동
- [x] Snapshot의 Collision 복원
- [x] Snapshot의 ReplicateMovement 복원
- [x] Snapshot의 MovementMode 복원
- [x] 기존 휴대 무기 `EquipCurrentWeapon()`
- [x] `MountedEnemy` 및 예약 상태 정리
- [x] AIController 이동 재개가 가능함을 확인

발사 루프와 Cannon 중립 조준은 마일스톤 8에서 실제 조준·발사 루프가 추가된 뒤 연결한다.

#### 네트워크 처리

- [x] `MountedEnemy` RepNotify에서 클라이언트 부착 상태 보정
- [x] 클라이언트에서도 이동 비활성화와 Collision 시각 상태 반영
- [x] 하차 RepNotify에서 클라이언트 상태 복원
- [ ] 서버 Attachment Replication과 Character ReplicateMovement가 충돌하지 않는지 PIE 확인

#### 충돌 정책

Enemy Actor 전체 Collision을 끄지 않는다.

권장 초기 정책:

```text
CharacterMovement: Disabled
Capsule: QueryOnly
ObjectType: Pawn 유지
Weapon Trace: 감지 가능
Visibility Trace: 감지 가능
대포/함선과 Physics Contact: 없음
```

#### 자동화/기능 테스트

- [x] 예약 Enemy만 Mount 가능
- [x] MountPoint 밖의 Enemy는 Mount 실패
- [x] Mount 후 Enemy가 걷거나 밀려나지 않음
- [x] 대포 Yaw 회전 시 Enemy도 함께 회전
- [ ] Mount 중 Player 공격에 Enemy가 피격됨
- [ ] AIController가 계속 Enemy를 Possess
- [x] Dismount 후 Enemy가 다시 이동 가능
- [ ] Dismount 후 기존 총/칼 Ability 복구

#### 구현 상태와 정책

2026-07-31 기준:

- `EnemyMountAcceptanceRadius` 기본값은 65cm이며 `BP_EnemyCannon`에서 조정할 수 있다.
- HostShip 판정은 Character Movement Base, Attach Parent 계층, Owner 순서로 확인한다.
- Mount 중에는 `ReservedEnemy`를 같은 Enemy로 유지하고 `MountedEnemy`도 설정한다.
- 정상 하차에서 `MountedEnemy`와 `ReservedEnemy`를 함께 정리하고 ReservationRevision을 갱신한다.
- Capsule의 기존 Collision Profile과 Response는 유지한 채 `QueryOnly`로 전환하므로 피격 Query가 유지된다.
- 정상 하차는 NavMesh 투영에 성공하면 투영 위치를, 실패하면 authored `EnemyDismountPoint`를 사용한다.
- 사망 또는 파괴 중인 Enemy는 Cannon 점유와 Attach만 정리하고 이동·충돌 상태를 강제로 되돌리지 않는다.
- 실제 Player 공격 피격, AIController Possess 유지, 무기 Ability 복구 및 네트워크 Attachment 충돌은 Editor PIE 확인 항목으로 남긴다.

#### 간단한 테스트 루프

자동화 테스트:

```text
Unreal Editor
-> Window
-> Test Automation
-> 검색: ArtisticSW.EnemyCannon.Milestone4
-> MountAndRestore 실행
-> ValidationAndDeathCleanup 실행
-> CannonEndPlayRestore 실행
```

Output Log 또는 명령행:

```text
Automation RunTests ArtisticSW.EnemyCannon.Milestone4
```

자동화 테스트 흐름:

```text
같은 Ship에 Enemy와 EnemyCannon 배치
-> Enemy 예약
-> Mount
-> MountPoint Snap, 이동 정지, QueryOnly, Tag 확인
-> Cannon 회전 시 Enemy 추종 확인
-> Dismount
-> Movement/Collision/ReplicateMovement/Attach Parent 복원 확인

다른 Ship의 Enemy 예약
-> Mount 실패
-> 같은 Ship으로 이동
-> Mount 반경 밖에서 실패
-> 반경 안에서 Mount 성공
-> Enemy 사망
-> Cannon Available 및 Attach/Tag 정리 확인

Enemy Mount 성공
-> Cannon Destroy
-> 살아 있는 Enemy의 Attach/Movement/Collision/ReplicateMovement 복원 확인
```

#### 에디터에서 해야 할 작업

1. C++ 빌드 후 Editor를 재시작한다.
2. `BP_EnemyCannon`을 열고 Parent Class가 `AEnemyCannon`인지 확인한다.
3. Components에서 `EnemyMountPoint`를 포수가 서거나 앉을 정확한 위치에 배치한다.
4. `EnemyMountPoint`의 정면 축이 Enemy가 바라볼 대포 정면과 일치하도록 Rotation을 조정한다.
5. `EnemyDismountPoint`를 갑판 위의 안전한 하차 위치에 배치한다. 난간 밖이나 Cannon 충돌 내부를 피한다.
6. Details의 `Enemy Cannon | Mount -> Enemy Mount Acceptance Radius`를 우선 65cm로 유지한다.
7. 테스트 레벨에서 `BP_EnemyCannon`이 대상 `Ship`에 Attach되거나 ChildActorComponent로 포함되어 있는지 확인한다.
8. 테스트 Enemy도 같은 Ship 위에 서 있거나 Ship에 Attach되어 HostShip 판정이 가능해야 한다.
9. Enemy를 MountPoint 65cm 안에 배치한다.
10. Level Blueprint 또는 임시 Debug Blueprint에서 다음 순서로 호출한다.

```text
Enemy
-> Get Cannon Operator Component
-> Try Reserve Cannon(BP_EnemyCannon)
-> Mount Reserved Cannon
```

11. PIE 중 다음 상태를 확인한다.

```text
EnemyCannon OperationState == EnemyMounted
Enemy MountedCannon == BP_EnemyCannon
Enemy CharacterMovement == None
Enemy Capsule CollisionEnabled == QueryOnly
Enemy ASC에 State.Operating.Cannon 존재
Cannon을 회전하면 Enemy도 함께 회전
```

12. `Dismount And Release`를 호출하고 다음을 확인한다.

```text
EnemyCannon OperationState == Available
Enemy가 EnemyDismountPoint 근처로 이동
이동 및 충돌 복원
기존 휴대 무기 재장착
State.Operating.Cannon 제거
```

13. Player 공격으로 탑승 Enemy가 피격되는지 확인한다.
14. Enemy 사망 시 즉시 Cannon이 `Available`로 돌아오는지 확인한다.
15. Listen Server 1명 + Client 1명 PIE에서 Client 화면의 Enemy Attach/Detach와 Cannon 회전을 확인한다.

#### 완료 조건

- Enemy가 대포에 붙은 상태로 이동하지 않는다.
- AIController와 Behavior Tree는 계속 Enemy를 제어한다.
- 하차 시 이동, 충돌, 무기 상태가 완전히 복원된다.

---

### 마일스톤 5: 포탄 발사자, 팀 판정 및 Player 피해

#### 목표

Enemy가 어떤 함선의 EnemyCannon을 사용하더라도 포탄이 실제 조작자 팀으로 판정되고 Player에게 GAS 피해를 줄 수 있도록 한다.

#### 수정 파일

```text
Source/WaterAndShip/Public/Cannon.h
Source/WaterAndShip/Private/Cannon.cpp
Source/WaterAndShip/Public/Cannonball.h
Source/WaterAndShip/Private/Cannonball.cpp
Source/Enemy/Public/Cannon/EnemyCannon.h
Source/Enemy/Private/Cannon/EnemyCannon.cpp
```

#### 개발 항목

- [x] 발사 Context 구조체 도입 여부 결정
- [x] 최소한 포탄 초기화 시 Operator 또는 SourceTeam 전달
- [x] `AEnemyCannon::ResolveFiringOperator()` 구현
- [x] `MountedEnemy`가 있으면 이를 발사 Instigator로 반환
- [x] MountedEnemy가 없으면 Base의 RidingPlayer를 사용
- [x] Operator가 없으면 기존 MountShip 팀 fallback 유지
- [x] Cannonball의 팀 판정을 LaunchingShip 단독 기준에서 변경
- [x] Enemy 포탄이 Pawn과 충돌하도록 Collision Response 확장
- [x] Character Hit 처리 함수 추가
- [x] 동일 팀 피해 방지
- [x] 발사 Enemy 자신 피해 방지
- [x] Player ASC에 Damage GameplayEffect 적용
- [x] Effect Context에 Instigator, EffectCauser, SourceObject, HitResult 설정
- [x] Ship Hit 파이프라인 회귀 방지

#### 권장 발사 Context

```cpp
USTRUCT()
struct FCannonFireContext
{
    GENERATED_BODY()

    UPROPERTY()
    TObjectPtr<AShip> MountShip = nullptr;

    UPROPERTY()
    TObjectPtr<APawn> Operator = nullptr;

    UPROPERTY()
    FGameplayTag SourceTeam;

    float Damage = 0.0f;
    float ProjectileSpeed = 0.0f;
};
```

#### 팀 판정

```text
Operator ASC 또는 TeamTag 존재
-> Operator 팀 사용

Operator 없음
-> MountShip 팀 사용

Target 팀 == Source 팀
-> 피해 무시
```

#### 구현 주의사항

- Player가 EnemyCannon을 사용하면 Player 포탄이어야 한다.
- Enemy가 Player 함선에 설치된 EnemyCannon을 사용하면 Enemy 포탄이어야 한다.
- 기존 EnemyShip AI는 Operator가 없을 수 있으므로 MountShip fallback이 필요하다.
- WaterAndShip에 `ABaseEnemy` 의존을 추가하지 않는다.
- Pawn Collision 추가로 포탄이 발사 직후 탑승 Enemy와 충돌하지 않도록 Operator를 Ignore한다.

#### 자동화/기능 테스트

- [ ] Player가 EnemyCannon 발사 시 Player 팀 포탄
- [ ] Enemy가 EnemyCannon 발사 시 Enemy 팀 포탄
- [ ] Enemy가 Player 함선의 Cannon 발사 시 Enemy 팀 포탄
- [ ] Enemy 포탄이 Player에게 피해
- [ ] Enemy 포탄이 같은 팀 Enemy에게 피해를 주지 않음
- [ ] 포탄이 탑승 Operator에게 피해를 주지 않음
- [ ] 기존 Ship 대 Ship 피해 유지
- [ ] Damage Instigator가 올바른 Enemy 또는 Player

#### 구현 상태와 테스트 루프

2026-07-31 기준 `FCannonFireContext`를 도입했다. `ACannon`은 실제 Operator의 ASC Team Tag를 우선 사용하고, Operator가 없으면 기존 Ship의 Enemy Actor Tag 또는 Player Ship fallback을 사용한다. `AEnemyCannon`은 MountedEnemy, RidingPlayer 순서로 발사자를 결정한다.

`ACannonball`은 Pawn 채널을 Block하고 Operator를 이동 Ignore 목록에 넣는다. Pawn 또는 Ship 명중 시 Source ASC에서 Damage Spec을 만들며 Instigator, Projectile EffectCauser/SourceObject와 Pawn HitResult를 Context에 기록한다. 같은 팀과 Operator 자신은 피해 대상에서 제외한다.

자동화 테스트:

```text
Automation RunTests ArtisticSW.EnemyCannon.Milestone5
Automation RunTests ArtisticSW.Cannon
```

`OperatorTeamAndPawnDamage`는 Enemy 팀 Context, Pawn 충돌, 같은 팀/자기 자신 제외, Player 팀 Pawn의 GAS Health 감소를 확인한다. `ArtisticSW.Cannon` 전체 테스트로 일반 Cannon 발사 경로의 회귀를 확인한다.

#### 완료 조건

- 발사 주체와 피해 팀이 MountShip이 아니라 실제 조작자 우선으로 결정된다.
- Player 대상 대포 공격이 실제 Damage로 연결된다.
- 기존 Ship Cannon 전투가 깨지지 않는다.

---

### 마일스톤 6: 대포 검색, 후보 평가 및 예약 BT

#### 목표

Player를 감지한 Enemy가 같은 HostShip의 EnemyCannon을 찾아 점수순으로 예약하도록 한다.

#### 새 파일

```text
Source/Enemy/Public/Task/BTT_FindAndReserveEnemyCannon.h
Source/Enemy/Private/Task/BTT_FindAndReserveEnemyCannon.cpp
Source/Enemy/Public/Decorator/BTD_HasValidEnemyCannonReservation.h
Source/Enemy/Private/Decorator/BTD_HasValidEnemyCannonReservation.cpp
```

#### Blackboard Key

```text
TargetActor                  Object/Actor
ReservedEnemyCannon         Object/AEnemyCannon
HasCannonReservation        Bool
IsMountedOnCannon           Bool
HasCannonAimSolution        Bool
CannonMountGoal             Vector
CannonInvalidTime           Float
```

#### 대포 검색

첫 구현에서는 HostShip에 부착된 Actor와 ChildActorComponent를 전투 진입 시 검색한다.

- [x] Enemy의 HostShip 결정
- [x] HostShip에 Attach된 `AEnemyCannon` 검색
- [x] HostShip의 ChildActorComponent 안 `AEnemyCannon` 검색
- [x] 다른 함선 Cannon 제외
- [x] null과 파괴 예정 Cannon 제외
- [x] Player 또는 다른 Enemy 사용 중 Cannon 제외
- [x] 검색을 BT Service Tick마다 수행하지 않음

대포 수나 Enemy 수가 실제 병목이 된 뒤에 Registry 또는 Subsystem을 고려한다.

#### 후보 필터

- [x] Enemy가 Cannon 사용 가능 유형
- [x] 같은 HostShip
- [x] Cannon 사용 가능 상태
- [x] Enemy에서 MountPoint까지 거리 제한
- [ ] 대략적인 이동 가능성
- [x] Target 유효 및 생존
- [ ] Target에 대한 정확한 탄도해 존재 (마일스톤 8)
- [x] 요구 Yaw/Pitch가 Cannon 제한 범위 안

#### 후보 점수

낮은 점수를 우선한다.

```text
Score =
    Enemy -> MountPoint 이동거리 * 0.45
  + 요구 Yaw 절대값 * 0.25
  + 요구 Pitch 부담 * 0.10
  + 예상 포탄 비행시간 * 0.10
  + 장애물 또는 위험 비용 * 0.10
```

#### 예약 순서

```text
후보 계산
-> Score 오름차순 정렬
-> 1순위 TryReserveForEnemy
-> 실패하면 2순위
-> 전부 실패하면 Task Failed
-> 기존 총/칼 전투 Branch로 fallback
```

예약 가능 여부 확인과 실제 예약 설정을 별도 호출로 분리하지 않는다. 최종 결정은 항상 서버의 `TryReserveForEnemy()`가 한다.

#### 자동화/기능 테스트

- [x] 대포 1개, Enemy 2명 중 1명만 예약 성공
- [x] 대포 2개, Enemy 2명이 서로 다른 대포 예약
- [x] 1순위 경쟁 실패 후 2순위 예약
- [x] 다른 함선 대포 제외
- [x] 모든 대포 실패 시 Task Failed 반환

#### 구현 상태와 테스트 루프

`BTT_FindAndReserveEnemyCannon`은 Target을 감지해 Cannon 전투 Branch에 진입할 때 한 번 후보를 검색한다. 같은 HostShip의 Attached Actor와 ChildActorComponent를 수집하고, 재구성 중인 ChildActor 누락을 보완하기 위해 World Actor도 같은 HostShip 조건으로 한 번 필터링한다. 거리 3000cm 이내, 사용 가능 상태, Target 방향의 Yaw/Pitch 제한을 통과한 후보를 점수순으로 예약한다.

정확한 탄도해와 Nav 경로 비용은 마일스톤 8의 탄도 계산 및 실제 레벨 수동 검증 범위다. 현재 점수는 이동거리, Yaw/Pitch 부담과 직선 비행시간 근사치를 사용한다.

자동화 테스트:

```text
Automation RunTests ArtisticSW.EnemyCannon.Milestone6
```

`CandidateCompetitionAndShipFilter`는 가까운 1순위 예약, 경쟁 실패 후 2순위 예약, 다른 Ship의 Cannon 제외를 한 루프에서 검증한다. Task는 후보가 없으면 `Failed`를 반환하므로 상위 Selector의 기존 총/칼 Branch로 fallback할 수 있다.

#### 완료 조건

- 다수 Enemy가 하나의 대포에 중복 예약되지 않는다.
- 대포가 없거나 사용할 수 없을 때 기존 전투가 계속된다.

---

### 마일스톤 7: 움직이는 대포까지 이동하고 탑승하는 BT

#### 목표

예약 Enemy가 움직이고 회전하는 함선 위에서 현재 Cannon MountPoint를 추적하여 도착하고, 도착 후 안전하게 Mount한다.

#### 새 파일

```text
Source/Enemy/Public/Task/BTT_MoveToReservedEnemyCannon.h
Source/Enemy/Private/Task/BTT_MoveToReservedEnemyCannon.cpp
Source/Enemy/Public/Task/BTT_MountReservedEnemyCannon.h
Source/Enemy/Private/Task/BTT_MountReservedEnemyCannon.cpp
```

#### 이동 Task 개발 항목

- [x] Blackboard의 ReservedEnemyCannon 읽기
- [x] Component의 ReservationId 유효성 확인
- [x] Cannon MountPoint의 현재 World Transform 조회
- [x] 이동 중 0.05~0.15초 간격으로 목표 갱신
- [x] 현재 목표를 Blackboard `CannonMountGoal`에 갱신
- [x] `MoveToLocation(..., bUsePathfinding=false)` MVP 구현
- [x] 같은 HostShip의 authored MountPoint만 목표로 허용
- [x] 대포 파괴/예약 상실 시 즉시 실패
- [x] 이동 Timeout 처리
- [x] Task Abort 시 예약 해제 정책 적용

#### 권장 초기 값

```text
Goal 갱신 간격: 0.1초
도착 허용 반경: 50cm
Mount 허용 반경: 65cm
이동 Timeout: 3초
```

#### Mount Task 개발 항목

- [x] 예약과 거리 재검증
- [x] Player가 중간에 Board하지 않았는지 확인
- [x] 다른 Enemy가 Mount하지 않았는지 확인
- [x] `UEnemyCannonOperatorComponent::MountReservedCannon()` 호출
- [x] 성공하면 Blackboard `IsMountedOnCannon = true`
- [x] 실패하면 예약 정리 후 Task Failed

#### 구현 주의사항

- MountPoint의 최초 World 위치를 고정 저장하지 않는다.
- 함선이 움직이므로 매 갱신 시 Component의 현재 World Transform을 읽는다.
- Enemy는 Mount 성공 전까지는 Cannon에 Attach하지 않는다.
- 이동 중 Target이 사라져 Combat Branch가 Abort되면 예약이 남지 않아야 한다.

#### 기능 테스트

- [x] 정지한 함선에서 MountPoint 도착 및 탑승 Core Loop
- [x] 이동 중인 함선에서 현재 MountPoint 재조회
- [x] 회전 중인 함선에서 현재 MountPoint 재조회
- [x] 이동 도중 Cannon 파괴/예약 상실 시 Task 실패 및 정리 경로
- [x] 이동 도중 Target 사망 시 Task 실패 및 정리 경로
- [x] 도착 후 Enemy가 Cannon에 정확히 Attach

#### 구현 상태와 테스트 루프

`BTT_MoveToReservedEnemyCannon`은 0.1초마다 현재 MountPoint를 읽어 `CannonMountGoal`과 AI 이동 요청을 갱신한다. 도착 반경은 50cm, Timeout은 3초이며 기본 Pathfinding은 꺼져 있다. 실패 또는 Abort 시 `DismountAndRelease()`로 예약을 정리한다. `BTT_MountReservedEnemyCannon`은 서버 예약, 점유 상태와 65cm Mount 반경을 다시 검증한 뒤 Attach한다.

자동화 테스트:

```text
Automation RunTests ArtisticSW.EnemyCannon.Milestone7
Automation RunTests ArtisticSW.EnemyCannon
```

`MovingGoalAndMount`는 Ship 위치와 회전을 바꾼 뒤 목표가 최초 위치로 캐시되지 않았는지 확인하고, 갱신된 목표에 Enemy를 도달시켜 정확한 Attach와 MountedEnemy 상태를 검증한다.

#### 에디터에서 자동 예약·이동·탑승 테스트 설정

`BP_Enemy`를 `BP_EnemyCannon` 옆에 두는 것만으로는 충분하지 않다. 아래 HostShip, Target, Blackboard와 Behavior Tree 연결이 모두 필요하다.

1. C++ 빌드 후 Editor를 완전히 재시작한다.
2. `BB_EnemyBase`에 아래 키를 정확한 이름과 타입으로 추가한다.

```text
TargetActor                  Object / Actor (기존 키 유지)
ReservedEnemyCannon         Object / AEnemyCannon
HasCannonReservation        Bool
IsMountedOnCannon           Bool
HasCannonAimSolution        Bool
CannonMountGoal             Vector
CannonInvalidTime           Float
```

3. `BT_EnemyBase`의 Target이 유효한 상위 Selector 안에 테스트용 Sequence를 최우선 자식으로 추가한다.
4. Sequence에 다음 순서로 노드를 연결한다.

```text
Find And Reserve Enemy Cannon
-> Move To Reserved Enemy Cannon
   + Has Valid Enemy Cannon Reservation Decorator
     Observer Aborts: Self
-> Mount Reserved Enemy Cannon
-> Wait(테스트용으로 999초)
```

5. 세 Task의 Blackboard Key가 위 이름으로 자동 선택됐는지 Details에서 확인한다. Search Distance는 우선 3000cm, Goal Update Interval 0.1초, Acceptance Radius 50cm, Move Timeout 3초, Use Pathfinding false를 유지한다.
6. 상위 Sequence에는 기존 `TargetActor Is Set` Decorator를 두고 `Observer Aborts: Self` 또는 `Both`를 사용한다. 이동 중 Target이 사라지면 Move Task Abort가 예약을 해제한다.
7. `BP_Enemy`의 Behavior Tree가 수정한 `BT_EnemyBase`인지, AI Controller Class가 `ABaseAIController` 계열인지, Auto Possess AI가 `Placed in World or Spawned`인지 확인한다.
8. `BP_EnemyCannon`의 Parent Class가 `AEnemyCannon`인지 확인하고 `EnemyMountPoint`를 포수 위치, `EnemyDismountPoint`를 갑판 안전 위치로 배치한다. Mount Acceptance Radius는 65cm로 둔다.
9. `BP_EnemyCannon`을 World Outliner에서 테스트 Ship 아래에 Attach하거나 Ship Blueprint의 ChildActorComponent로 포함한다. 단순히 Ship 근처에 독립 배치하면 같은 HostShip Cannon으로 검색되지 않는다.
10. `BP_Enemy`를 같은 Ship 갑판 위에 배치한다. 가장 확실한 첫 테스트는 World Outliner에서 같은 Ship에 Attach한 상태로 시작하는 것이다. 실제 플레이 배치에서는 Character Movement Base가 Ship으로 잡혀도 HostShip 판정이 된다.
11. Cannon의 로컬 +X 전방 기준 ±60도 안에 Player를 두고, Enemy의 기본 SightRadius 1000cm 안에 배치한다. Cannon 후보 검색 거리도 3000cm 이내여야 한다.
12. 장애물이 없는 갑판에서 시작한다. 현재 MVP 이동은 Pathfinding false이므로 난간, 벽 또는 복잡한 계단을 통과하는 테스트는 후속 Nav 검증으로 분리한다.
13. PIE는 우선 `Selected Viewport`, Players 1, Net Mode `Play As Listen Server`로 시작한다.
14. Behavior Tree Debug에서 해당 Enemy 인스턴스를 선택하고 다음 변화를 순서대로 확인한다.

```text
TargetActor = Player
ReservedEnemyCannon = 배치한 BP_EnemyCannon
HasCannonReservation = true
CannonMountGoal = 움직이는 MountPoint 현재 위치
Enemy가 목표로 이동
IsMountedOnCannon = true
EnemyCannon OperationState = EnemyMounted
Enemy CharacterMovement = None
Enemy ASC에 State.Operating.Cannon 존재
```

15. 경쟁 테스트는 같은 Ship에 Cannon 1개와 Enemy 2명을 두고 PIE한다. 한 명만 예약·탑승해야 하며 다른 Enemy의 Find Task는 실패해 상위 Selector의 기존 전투 Branch로 내려가야 한다.
16. 이동 추적 테스트는 먼저 정지 Ship에서 통과시킨 뒤 Ship을 천천히 직선 이동·회전시킨다. Blackboard의 `CannonMountGoal`이 0.1초마다 변하고 Enemy가 현재 위치로 접근하는지 확인한다.

마일스톤 7까지는 자동 조준·반복 발사와 정상 하차 BT가 아직 없다. 위의 긴 Wait는 탑승 상태 관찰용이며, 테스트 종료는 PIE Stop으로 한다. 실전용 Cannon 운용·하차 Branch는 마일스톤 8과 9에서 연결한다.

#### 완료 조건

- 움직이는 함선에서도 Enemy가 현재 MountPoint에 도달한다.
- Mount 성공 후 Enemy 이동이 완전히 정지한다.
- 실패와 Abort 경로에서 예약 누수가 없다.

---

### 마일스톤 8: AI 탄도 조준과 발사 반복

#### 목표

탑승 Enemy가 Player의 현재 및 예상 위치에 맞춰 Cannon을 조준하고, 유효한 조준 상태에서 기존 Cannon 발사 파이프라인을 호출한다.

#### 새 파일

```text
Source/Enemy/Public/Task/BTT_OperateEnemyCannon.h
Source/Enemy/Private/Task/BTT_OperateEnemyCannon.cpp
Source/Enemy/Public/Service/BTS_UpdateEnemyCannonContext.h
Source/Enemy/Private/Service/BTS_UpdateEnemyCannonContext.cpp
```

#### 수정 파일

```text
Source/Enemy/Public/Cannon/EnemyCannon.h
Source/Enemy/Private/Cannon/EnemyCannon.cpp
```

#### `AEnemyCannon` 권장 API

```cpp
bool TryCalculateAimAtTarget(
    const AActor* Target,
    FCannonAimRotation& OutAim,
    float& OutFlightTime) const;

bool ApplyEnemyAim(
    const FCannonAimRotation& DesiredAim,
    float DeltaSeconds);

bool IsAimWithinFireTolerance(
    const FCannonAimRotation& DesiredAim) const;

bool TryFireByMountedEnemy(
    ABaseEnemy* RequestingEnemy);
```

#### 탄도 계산

- [ ] Muzzle 기준 시작 위치 사용
- [ ] 실제 CannonballSpeed 조회
- [ ] World Gravity 사용
- [ ] 저각 탄도해 계산
- [ ] 탄도해가 없으면 실패
- [ ] 요구 Pitch가 MinPitch/MaxPitch 안인지 확인
- [ ] 요구 Yaw가 MaxYawOffset 안인지 확인
- [ ] 예상 비행시간 계산
- [ ] `TargetLocation + TargetVelocity * FlightTime`으로 1차 Lead
- [ ] Lead 위치에 대해 탄도해 재계산
- [ ] 필요하면 최대 2회 반복

#### 조준 보간

```text
Yaw 회전 속도: 40~70도/초
Pitch 회전 속도: 25~45도/초
발사 허용 Yaw 오차: 2~5도
발사 허용 Pitch 오차: 2~4도
```

- [ ] `SetAIAimRotation()`에 즉시 Snap하지 않고 보간
- [ ] 조준 불가능한 각도를 Clamp한 뒤 발사하지 않음
- [ ] 원래 요구 각도가 제한 밖이면 명시적으로 실패

#### 발사 검증

- [ ] 서버 권한
- [ ] `MountedEnemy == RequestingEnemy`
- [ ] Player Rider 없음
- [ ] Target 유효 및 생존
- [ ] 같은 전투 갑판 또는 허용된 Target
- [ ] 현재 탄도해 유효
- [ ] 조준 오차 허용 범위 이내
- [ ] Cannon Disabled가 아님
- [ ] 기존 `FireCannon()`이 쿨다운을 최종 결정

#### Operate Task 흐름

```text
Mount 상태 확인
-> Target 확인
-> 탄도해 계산
-> 조준각 보간
-> 허용 오차 도달
-> TryFireByMountedEnemy
-> Cannon 쿨다운 동안 계속 추적 조준
-> 다시 발사
```

#### Ability 정책

MVP에서는 별도 Enemy Cannon GameplayAbility를 만들지 않는다.

```text
BTT_OperateEnemyCannon
-> AEnemyCannon AI API
-> 상속받은 ACannon::FireCannon
```

추후 Montage, GameplayTag 기반 취소, 재장전 GameplayCue가 필요할 때만 `GA_OperateCannon`을 추가한다. 추가하더라도 포탄 생성과 쿨다운은 `ACannon`에 남긴다.

#### 자동화/기능 테스트

- [ ] 정지 Player 조준 및 발사
- [ ] 이동 Player Lead 조준
- [ ] 최대 Yaw 밖 Target에 발사하지 않음
- [ ] 최대 Pitch 밖 Target에 발사하지 않음
- [ ] 쿨다운보다 빠르게 발사하지 않음
- [ ] 다른 Enemy가 탑승 Enemy 명의로 발사 요청할 수 없음
- [ ] Player가 조작할 때 기존 입력 발사 정상

#### 완료 조건

- 탑승 Enemy는 움직이지 않고 Cannon만 조작한다.
- 유효한 탄도해와 조준 오차 조건을 만족할 때만 발사한다.
- 기존 Cannon 쿨다운과 함선 Attribute를 그대로 사용한다.

---

### 마일스톤 9: 하차, Lease, 실패 복구 및 기존 전투 복귀

#### 목표

모든 정상·비정상 종료 경로에서 Enemy를 안전하게 하차시키고 예약을 해제하며 기존 총/칼 전투로 복귀시킨다.

#### 새 파일

```text
Source/Enemy/Public/Task/BTT_DismountEnemyCannon.h
Source/Enemy/Private/Task/BTT_DismountEnemyCannon.cpp
```

#### 예약 Lease

권장 초기 값:

```text
Heartbeat 주기: 0.5초
예약 Lease 만료: 3초
최소 Cannon 유지 시간: 1.5초
탄도해 없음 허용 시간: 1초
```

#### 강제 하차 조건

- [ ] MountedEnemy 사망
- [ ] Target 사망
- [ ] Target이 허용된 전투 공간을 떠남
- [ ] Cannon 파괴
- [ ] HostShip 파괴
- [ ] Cannon Disabled
- [ ] BT Combat Branch Abort
- [ ] Enemy Stun 또는 강제 행동 불가
- [ ] 일정 시간 탄도해 없음
- [ ] Lease Heartbeat 만료

#### 복귀 흐름

```text
발사 루프 중단
-> Aim 중립화
-> Enemy Detach
-> 안전한 Dismount 위치 결정
-> 이동/충돌/복제 Snapshot 복원
-> 휴대 무기 Equip
-> 예약 및 Mounted 상태 해제
-> Blackboard Cannon Key Clear
-> 기존 Portable Weapon Combat Branch 실행
```

#### 구현 주의사항

- Cannon 쿨다운은 하차 사유가 아니다.
- 잠깐 시야가 가려진 것만으로 즉시 하차하지 않는다.
- Target 손실 Grace Time을 둔다.
- Enemy가 죽은 경우 상태 복원보다 Cannon 예약 해제를 우선한다.
- DismountPoint가 갑판 밖이면 가장 가까운 안전 위치로 투영한다.
- Release와 Dismount는 여러 번 호출되어도 안전해야 한다.

#### 자동화/기능 테스트

- [ ] Enemy 사망 즉시 Cannon 사용 가능 상태 복구
- [ ] Target 사망 시 하차
- [ ] Target 손실 Grace Time 후 하차
- [ ] Cannon 파괴 시 Enemy 상태 복구
- [ ] BT Abort 시 예약 및 Attach 해제
- [ ] 하차 후 기존 `BTT_EnemyBasicAttack` 실행 가능
- [ ] 오래된 Lease가 자동 만료

#### 완료 조건

- 영구 예약과 영구 이동 불가 상태가 발생하지 않는다.
- 하차 후 Enemy가 기존 전투 루프로 정상 복귀한다.

---

### 마일스톤 10: Behavior Tree 통합, 애니메이션 및 Debug

#### 목표

EnemyCannon Branch를 전체 갑판 전투 Behavior Tree에 통합하고, 상태를 시각적으로 확인할 수 있도록 한다.

#### Content

권장 경로:

```text
Content/GameplayAbilitySystem/Enemy/DeckAI/BB_DeckEnemy
Content/GameplayAbilitySystem/Enemy/DeckAI/BT_DeckEnemy
Content/GameplayAbilitySystem/Enemy/DeckAI/BP_EnemyCannon
```

실제 프로젝트 Content 구조에 맞춰 조정하되 관련 Asset을 한 폴더에 모은다.

#### 권장 BT 구조

```text
Priority Selector
├─ Dead / Disabled
│  └─ Dismount And Release Cannon
├─ Recover To Deck
│  └─ Dismount And Release Cannon
├─ Valid Target
│  └─ Selector
│     ├─ Enemy Cannon Combat
│     │  ├─ Mounted: Operate Cannon
│     │  ├─ Reserved: Move -> Mount
│     │  └─ No Reservation: Find And Reserve
│     ├─ Existing Portable Weapon Combat
│     │  ├─ Chase / Reposition
│     │  └─ BTT_EnemyBasicAttack
│     └─ Wait / Investigate
└─ Patrol
```

#### GameplayTag

권장 Tag:

```text
State.Operating.Cannon
```

필요하면 다음도 추가한다.

```text
State.Reserving.Cannon
State.MovingTo.Cannon
```

Tag 선언 위치는 현재 프로젝트의 Native GameplayTag 관리 방식을 따른다.

#### Animation

- [ ] Cannon 탑승 Idle Pose
- [ ] BaseMesh 회전에 따른 Enemy 자연스러운 회전
- [ ] 필요하면 손 IK
- [ ] Fire 시 반동 Animation 또는 Montage
- [ ] 하차 Transition
- [ ] 사망 시 Attach 해제 후 기존 사망 Animation

MVP에서는 고정 Pose만으로 시작하고 IK와 정교한 Montage는 후속 단계로 둔다.

#### Debug

권장 표시:

```text
Cannon OperationState
ReservedEnemy 이름
MountedEnemy 이름
ReservationRevision
Lease 남은 시간
MountPoint
DismountPoint
현재 Target
Desired Pitch/Yaw
Current Pitch/Yaw
Aim Solution 성공 여부
발사 쿨다운 상태
```

권장 색:

```text
초록: Available
노랑: ReservedForEnemy
빨강: EnemyMounted
파랑: PlayerControlled
회색: Disabled
```

#### 완료 조건

- Cannon Branch와 기존 휴대 무기 Branch가 자연스럽게 전환된다.
- Debug 표시만 보고 예약 누수와 조준 실패 원인을 판단할 수 있다.
- 최소 탑승 Pose가 적용된다.

---

### 마일스톤 11: 네트워크, 성능 및 최종 회귀

#### 목표

멀티플레이 환경에서 Server Authority, Attachment, Cannon 회전, 포탄 피해와 상태 복구가 일치하는지 검증한다.

#### 서버 권한 대상

다음은 서버만 결정한다.

- Target 선정
- Cannon 후보 평가 최종 결과
- 예약 성공/해제
- Enemy Mount/Dismount
- Cannon Aim authoritative state
- Cannon 발사
- 포탄 Spawn
- Damage 적용
- Lease 만료

클라이언트는 다음 결과를 표현한다.

- Enemy Attach
- Cannon 회전
- 탑승 Animation
- 포탄과 VFX
- Damage 결과
- Debug 표시

#### 네트워크 테스트

- [ ] Listen Server + Client
- [ ] Dedicated Server + Client 2명
- [ ] Player 한 명이 EnemyCannon 사용
- [ ] Enemy 한 명이 EnemyCannon 사용
- [ ] Player와 Enemy가 동시에 같은 Cannon 사용 시도
- [ ] Enemy 두 명이 동시에 같은 Cannon 예약 시도
- [ ] Mount 중 함선 급회전
- [ ] Mount 중 Enemy 사망
- [ ] Cannon 파괴
- [ ] 100ms/200ms 지연
- [ ] Packet Loss 환경
- [ ] 늦게 접속한 Client에서 MountedEnemy 상태 확인

#### 성능 확인

- Cannon 후보 전체 검색을 Tick마다 하지 않는다.
- 탄도 계산은 Mounted Enemy 또는 후보 평가 시 제한된 빈도로 실행한다.
- Debug Draw는 CVar 또는 개발 설정으로만 활성화한다.
- Component와 예약 시스템은 Tick을 기본적으로 사용하지 않는다.
- 다수 Enemy 테스트 전에는 불필요한 Subsystem을 미리 만들지 않는다.

#### 최종 회귀

- [ ] 일반 `ACannon` Player Board/Exit
- [ ] `AEnemyCannon` Player Board/Exit
- [ ] EnemyShip 자동 Cannon
- [ ] Water Bomb Cannon
- [ ] Ship 대 Ship 포탄 피해
- [ ] Enemy Cannon 대 Player 피해
- [ ] 기존 총/칼 Enemy 공격
- [ ] Enemy 사망/드랍/HealthBar
- [ ] 함선 이동 및 Network Physics

#### 완료 조건

- 서버와 모든 Client에서 Cannon 점유자가 일치한다.
- Enemy Attachment와 Dismount 상태가 일치한다.
- 중복 발사와 중복 Damage가 없다.
- 기존 Cannon, EnemyShip, Water Bomb, 휴대 무기 전투에 회귀가 없다.

---

## 7. 구현 순서 요약

아래 순서를 유지한다.

```text
0. 기존 Cannon 기준선 확인
1. ACannon virtual hook
2. AEnemyCannon 예약/상태
3. EnemyCannonOperatorComponent
4. Enemy Attach/Dismount
5. 발사 Operator/팀/Player Damage
6. Cannon 검색/후보 평가/예약 BT
7. 움직이는 Cannon까지 이동/Mount BT
8. 탄도 조준/발사 BT
9. Lease/실패 복구/기존 전투 복귀
10. BT 통합/애니메이션/Debug
11. 네트워크/성능/최종 회귀
```

마일스톤 1~5는 Cannon과 탑승 파이프라인의 기반이므로 먼저 끝낸다. AI 검색과 BT 작업은 그 이후에 연결한다.

---

## 8. 최소 기능 슬라이스

전체 기능을 한 번에 구현하지 말고 다음 조합으로 첫 검증을 수행한다.

```text
장애물 없는 정지 함선 1척
BP_EnemyCannon 1개
Player 1명
Enemy 1명
Server Authority PIE
```

검증 순서:

1. Enemy가 Cannon 예약
2. Enemy가 MountPoint까지 직선 이동
3. Enemy가 Cannon에 Attach
4. Enemy 이동 정지
5. AIController가 Enemy를 계속 Possess
6. Cannon이 정지 Player를 조준
7. 포탄 발사
8. Player GAS Damage
9. Enemy 강제 하차
10. 기존 무기 Equip 및 이동 복구
11. Player가 같은 EnemyCannon을 Board하여 사용

이 기능 슬라이스가 통과한 후 다수 Enemy, 이동하는 배, Target Lead, Lease와 네트워크 지연을 추가한다.

---

## 9. 최종 테스트 매트릭스

| 시나리오 | 기대 결과 |
|---|---|
| EnemyCannon 1개, Enemy 2명 | 한 명만 예약, 다른 Enemy는 기존 무기 사용 |
| EnemyCannon 2개, Enemy 3명 | 두 명은 서로 다른 Cannon, 한 명은 기존 무기 사용 |
| Player가 Cannon 조작 중 | Enemy 예약 실패 |
| Enemy가 예약 중 | Player Board 실패 |
| Enemy가 Mount 중 | 다른 Enemy와 Player 사용 실패 |
| Enemy 사망 | 즉시 Dismount 및 예약 해제 |
| Cannon 파괴 | Enemy 상태 복원, 예약 해제 |
| Target 사망 | Grace Time 후 Dismount |
| Target이 조준 범위 밖 | 발사하지 않고 일정 시간 후 Dismount |
| Target이 이동 | Lead Aim 적용 |
| 함선 이동 및 회전 | Enemy가 MountPoint를 추적하고 Mount 후 함께 움직임 |
| Enemy가 Player 함선 Cannon 사용 | Enemy 팀 포탄 |
| Player가 EnemyCannon 사용 | Player 팀 포탄 |
| Enemy 포탄이 Player 명중 | Player GAS Damage |
| Enemy 포탄이 같은 팀 Enemy 명중 | 피해 없음 |
| Dismount 후 | 이동, Collision, Replication, 기존 무기 복구 |
| BT Abort | 예약과 Attach 누수 없음 |
| Dedicated Server 2 Client | 점유, 회전, 발사, Damage 상태 일치 |

---

## 10. 구현 중 피해야 할 방식

- 대포를 `UBaseWeaponComponent::CurrentWeapon`으로 설정하지 않는다.
- Cannon 발사와 쿨다운 코드를 `AEnemyCannon`에 복사하지 않는다.
- Enemy AIController가 Cannon을 Possess하게 만들지 않는다.
- Enemy Actor Collision을 완전히 끄지 않는다.
- 예약 확인과 예약 설정을 서로 다른 함수로 처리하지 않는다.
- MountPoint의 World 위치를 한 번만 저장하지 않는다.
- 대포가 쿨다운이라는 이유로 예약을 해제하지 않는다.
- 하차 시 Collision과 Movement 값을 하드코딩하지 않는다.
- 포탄 팀을 MountShip만으로 결정하지 않는다.
- `WaterAndShip` 모듈에서 `ABaseEnemy`를 참조하지 않는다.
- Cannon 전체 검색이나 탄도 계산을 모든 Enemy Tick마다 실행하지 않는다.
- Player Cannon 기능을 자식 클래스에 복사하여 별도 구현하지 않는다.

---

## 11. Definition of Done

다음 조건을 모두 만족하면 EnemyCannon 첫 버전을 완료한 것으로 본다.

- `AEnemyCannon`이 지정된 파일 경로에 구현되어 있다.
- Player가 `AEnemyCannon`을 기존 Cannon처럼 Board하고 사용할 수 있다.
- Enemy와 Player의 Cannon 점유가 상호 배타적이다.
- 여러 Enemy가 하나의 Cannon을 동시에 예약할 수 없다.
- Enemy는 Cannon까지 이동한 후 MountPoint에 Attach된다.
- Mount된 Enemy는 CharacterMovement가 정지한다.
- AIController는 Mount 중에도 Enemy를 계속 Possess한다.
- Cannon 회전에 따라 Enemy도 함께 회전한다.
- Mount된 Enemy는 Player 공격에 피격될 수 있다.
- Enemy는 Player 위치를 기반으로 Cannon을 조준하고 발사한다.
- Enemy 포탄은 Player에게 GAS Damage를 적용한다.
- 포탄 팀과 Instigator가 실제 Operator 기준으로 결정된다.
- Enemy 사망, Target 상실, Cannon 파괴, BT Abort 시 안전하게 Dismount한다.
- Dismount 후 Enemy 이동, Collision, Replication과 기존 무기가 복원된다.
- Cannon을 사용할 수 없는 Enemy는 기존 총/칼 전투를 수행한다.
- 기존 `ACannon`, EnemyShip Cannon, Water Bomb 기능에 회귀가 없다.
- Dedicated Server 환경에서 점유, Attach, Aim, Fire, Damage가 일치한다.

---

## 12. 다른 작업 채팅에서 시작할 때의 체크리스트

새 작업 채팅에서는 다음 순서로 시작한다.

1. 이 문서를 전체 읽기
2. 현재 구현 완료 마일스톤 확인
3. `git status`로 사용자 변경사항 확인
4. 현재 마일스톤의 관련 기존 파일 읽기
5. 이전 마일스톤 완료 조건 재검증
6. 해당 마일스톤만 구현
7. 컴파일 및 관련 자동화 테스트 실행
8. PIE 수동 검증이 필요한 항목 기록
9. 완료된 체크박스와 실제 구현 차이를 문서에 반영
10. 다음 마일스톤으로 이동

마일스톤을 건너뛸 경우 의존하는 API와 완료 조건이 이미 충족되었는지 먼저 확인한다.
