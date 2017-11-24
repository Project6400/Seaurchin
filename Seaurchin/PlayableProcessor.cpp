#include "ScoreProcessor.h"
#include "ExecutionManager.h"
#include "ScenePlayer.h"
#include "Misc.h"
#include "Debug.h"

using namespace std;

PlayableProcessor::PlayableProcessor(ScenePlayer * player)
{
    Player = player;
    CurrentState = Player->manager->GetControlStateSafe();
    SetJudgeWidths(0.033, 0.066, 0.084);
    SetJudgeAdjusts(0, 0, 1);
}

PlayableProcessor::PlayableProcessor(ScenePlayer * player, bool autoAir) : PlayableProcessor(player)
{
    Player = player;
    CurrentState = Player->manager->GetControlStateSafe();
    SetJudgeWidths(0.033, 0.066, 0.084);
    SetJudgeAdjusts(0, 0, 1);
    isAutoAir = autoAir;
}

void PlayableProcessor::SetAutoAir(bool flag)
{
    isAutoAir = flag;
}

void PlayableProcessor::SetJudgeWidths(double jc, double j, double a)
{
    judgeWidthAttack = a;
    judgeWidthJustice = j;
    judgeWidthJusticeCritical = jc;
}

void PlayableProcessor::SetJudgeAdjusts(double jas, double jaa, double jma)
{
    judgeAdjustSlider = jas;
    judgeAdjustAirString = jaa;
    judgeMultiplierAir = jma;
}

void PlayableProcessor::Reset()
{
    data = Player->data;
    Status.JusticeCritical = Status.Justice = Status.Attack = Status.Miss = Status.Combo = Status.CurrentGauge = 0;
    Status.AllNotes = 0;
    for (auto &note : data) {
        auto type = note->Type.to_ulong();
        if (type & SU_NOTE_LONG_MASK) {
            if (!note->Type.test((size_t)SusNoteType::AirAction)) Status.AllNotes++;
            for (auto &ex : note->ExtraData)
                if (
                    ex->Type.test((size_t)SusNoteType::End)
                    || ex->Type.test((size_t)SusNoteType::Step)
                    || ex->Type.test((size_t)SusNoteType::Injection))
                    Status.AllNotes++;
        } else if (type & SU_NOTE_SHORT_MASK) {
            Status.AllNotes++;
        }
    }

    imageHoldLight = dynamic_cast<SImage*>(Player->resources["LaneHoldLight"]);
}

void PlayableProcessor::Update(vector<shared_ptr<SusDrawableNoteData>>& notes)
{
    bool SlideCheck = false;
    bool HoldCheck = false;
    isInHold = false;
    for (auto& note : notes) {
        ProcessScore(note);
        SlideCheck = isInSlide || SlideCheck;
        HoldCheck = isInHold || HoldCheck;
    }

    if (!wasInSlide && SlideCheck) Player->PlaySoundSlide();
    if (wasInSlide && !SlideCheck) Player->StopSoundSlide();
    if (!wasInHold && HoldCheck) Player->PlaySoundHold();
    if (wasInHold && !HoldCheck) Player->StopSoundHold();

    wasInHold = HoldCheck;
    wasInSlide = SlideCheck;
}

void PlayableProcessor::MovePosition(double relative)
{
    double newTime = Player->CurrentSoundTime + relative;
    Status.JusticeCritical = Status.Justice = Status.Attack = Status.Miss = Status.Combo = Status.CurrentGauge = 0;

    wasInHold = isInHold = false;
    wasInSlide = isInSlide = false;
    Player->StopSoundHold();
    Player->StopSoundSlide();
    Player->RemoveSlideEffect();

    // ����: ��΂���������Finished��
    // �߂�: �����Ă��镔����Un-Finished��
    for (auto &note : data) {
        if (note->Type.test((size_t)SusNoteType::Hold)
            || note->Type.test((size_t)SusNoteType::Slide)
            || note->Type.test((size_t)SusNoteType::AirAction)) {
            if (note->StartTime <= newTime) note->OnTheFlyData.set((size_t)NoteAttribute::Finished);
            for (auto &extra : note->ExtraData) {
                if (extra->Type.test((size_t)SusNoteType::Invisible)) continue;
                if (extra->Type.test((size_t)SusNoteType::Control)) continue;
                if (relative >= 0) {
                    if (extra->StartTime <= newTime) note->OnTheFlyData.set((size_t)NoteAttribute::Finished);
                } else {
                    if (extra->StartTime >= newTime) note->OnTheFlyData.reset((size_t)NoteAttribute::Finished);
                }
            }
        } else {
            if (relative >= 0) {
                if (note->StartTime <= newTime) note->OnTheFlyData.set((size_t)NoteAttribute::Finished);
            } else {
                if (note->StartTime >= newTime) note->OnTheFlyData.reset((size_t)NoteAttribute::Finished);
            }
        }
    }
}

void PlayableProcessor::Draw()
{
    if (!imageHoldLight) return;
    SetDrawBlendMode(DX_BLENDMODE_ALPHA, 255);
    for (int i = 0; i < 16; i++)
        if (CurrentState->GetCurrentState(ControllerSource::IntegratedSliders, i))
            DrawRectRotaGraph3F(
                Player->widthPerLane * i, Player->laneBufferY,
                0, 0,
                imageHoldLight->get_Width(), imageHoldLight->get_Height(),
                0, imageHoldLight->get_Height(),
                1, 2, 0,
                imageHoldLight->GetHandle(), TRUE, FALSE);
}

PlayStatus *PlayableProcessor::GetPlayStatus()
{
    return &Status;
}

void PlayableProcessor::ProcessScore(shared_ptr<SusDrawableNoteData> note)
{
    if (note->OnTheFlyData.test((size_t)NoteAttribute::Finished) && note->ExtraData.size() == 0) return;
    auto state = note->Type.to_ulong();

    if (note->Type.test((size_t)SusNoteType::Hold)) {
        isInHold = CheckHoldJudgement(note);
    } else if (note->Type.test((size_t)SusNoteType::Slide)) {
        isInSlide = CheckSlideJudgement(note);
    } else if (note->Type.test((size_t)SusNoteType::AirAction)) {
        CheckAirActionJudgement(note);
    } else if (note->Type.test((size_t)SusNoteType::Air)) {
        if (!CheckAirJudgement(note)) return;
        Player->PlaySoundAir();
        Player->SpawnJudgeEffect(note, JudgeType::ShortNormal);
        Player->SpawnJudgeEffect(note, JudgeType::ShortEx);
    } else if (note->Type.test((size_t)SusNoteType::Tap)) {
        if (!CheckJudgement(note)) return;
        Player->PlaySoundTap();
        Player->SpawnJudgeEffect(note, JudgeType::ShortNormal);
    } else if (note->Type.test((size_t)SusNoteType::ExTap)) {
        if (!CheckJudgement(note)) return;
        Player->PlaySoundExTap();
        Player->SpawnJudgeEffect(note, JudgeType::ShortNormal);
        Player->SpawnJudgeEffect(note, JudgeType::ShortEx);
    } else if (note->Type.test((size_t)SusNoteType::Flick)) {
        if (!CheckJudgement(note)) return;
        Player->PlaySoundFlick();
        Player->SpawnJudgeEffect(note, JudgeType::ShortNormal);
    } else {
        // Hell
        if (!CheckHellJudgement(note)) return;
        Player->PlaySoundTap();
        Player->SpawnJudgeEffect(note, JudgeType::ShortNormal);
    }
}


bool PlayableProcessor::CheckJudgement(shared_ptr<SusDrawableNoteData> note)
{
    double reltime = Player->CurrentTime - note->StartTime - judgeAdjustSlider;
    if (note->OnTheFlyData.test((size_t)NoteAttribute::Finished)) return false;
    if (reltime < -judgeWidthAttack) return false;
    if (reltime > judgeWidthAttack) {
        ResetCombo(note);
        return false;
    }
    for (int i = note->StartLane; i < note->StartLane + note->Length; i++) {
        if (!CurrentState->GetTriggerState(ControllerSource::IntegratedSliders, i)) continue;
        if (note->Type[(size_t)SusNoteType::ExTap]) {
            IncrementComboEx(note);
        } else {
            IncrementCombo(note, reltime);
        }
        return true;
    }
    return false;
}

bool PlayableProcessor::CheckHellJudgement(shared_ptr<SusDrawableNoteData> note)
{
    double reltime = Player->CurrentTime - note->StartTime - judgeAdjustSlider;
    if (note->OnTheFlyData.test((size_t)NoteAttribute::Finished)) return false;
    if (reltime < -judgeWidthAttack) return false;
    if (reltime > judgeWidthAttack) {
        IncrementComboHell(note, -1);
        return false;
    }
    if (reltime >= 0 && !note->OnTheFlyData.test((size_t)NoteAttribute::HellChecking)) {
        IncrementComboHell(note, 0);
        return true;
    }

    for (int i = note->StartLane; i < note->StartLane + note->Length; i++) {
        if (!CurrentState->GetTriggerState(ControllerSource::IntegratedSliders, i)) continue;
        if (reltime >= 0) {
            IncrementComboHell(note, 1);
        } else {
            IncrementComboHell(note, 2);
        }
        return false;
    }
    return false;
}

bool PlayableProcessor::CheckAirJudgement(shared_ptr<SusDrawableNoteData> note)
{
    double reltime = Player->CurrentTime - note->StartTime - judgeAdjustAirString;
    reltime /= judgeMultiplierAir;
    if (note->OnTheFlyData.test((size_t)NoteAttribute::Finished)) return false;
    if (reltime < -judgeWidthAttack) return false;
    if (reltime > judgeWidthAttack) {
        ResetCombo(note);
        return false;
    }

    if (isAutoAir && reltime < 0) return false;
    bool judged = CurrentState->GetTriggerState(
        ControllerSource::IntegratedAir,
        note->Type[(size_t)SusNoteType::Up] ? (int)AirControlSource::AirUp : (int)AirControlSource::AirDown);
    if (!isAutoAir && !judged) return false;

    IncrementComboAir(note, reltime);
    return true;
}

bool PlayableProcessor::CheckHoldJudgement(shared_ptr<SusDrawableNoteData> note)
{
    double reltime = Player->CurrentTime - note->StartTime - judgeAdjustSlider;
    if (reltime < -judgeWidthAttack) return false;
    if (reltime >= note->Duration + judgeWidthAttack) return false;
    if (note->OnTheFlyData[(size_t)NoteAttribute::Completed]) return false;

    //���݂̔���ʒu�𒲂ׂ�
    int left = note->StartLane;
    int right = left + note->Length;
    // left <= i < right �Ŕ���
    bool held = false, trigger = false, release = false;
    for (int i = note->StartLane; i < (note->StartLane + note->Length); i++) {
        held |= CurrentState->GetCurrentState(ControllerSource::IntegratedSliders, i);
        trigger |= CurrentState->GetTriggerState(ControllerSource::IntegratedSliders, i);
        release |= CurrentState->GetLastState(ControllerSource::IntegratedSliders, i) && !CurrentState->GetCurrentState(ControllerSource::IntegratedSliders, i);
    }
    double judgeTime = Player->CurrentTime - note->StartTime - judgeAdjustSlider;

    // Start����
    if (!note->OnTheFlyData[(size_t)NoteAttribute::Finished]) {
        if (reltime >= judgeWidthAttack) {
            ResetCombo(note);
        } else if (trigger) {
            IncrementCombo(note, judgeTime);
            Player->PlaySoundTap();
            Player->SpawnJudgeEffect(note, JudgeType::ShortNormal);
        }
        return held;
    }

    // Step~End����
    for (const auto &extra : note->ExtraData) {
        judgeTime = Player->CurrentSoundTime - extra->StartTime - judgeAdjustSlider;
        if (extra->OnTheFlyData[(size_t)NoteAttribute::Finished]) continue;
        if (extra->Type[(size_t)SusNoteType::Control]) continue;
        if (extra->Type[(size_t)SusNoteType::Invisible]) continue;

        if (judgeTime < -judgeWidthAttack) {
            return held;
        } else if (judgeTime >= judgeWidthAttack) {
            ResetCombo(extra);
            return held;
        } else if (judgeTime >= 0 && held) {
            if (extra->Type[(size_t)SusNoteType::Injection]) {
                IncrementCombo(extra, judgeTime);
            } else if (extra->Type[(size_t)SusNoteType::Step]) {
                IncrementCombo(extra, judgeTime);
                Player->PlaySoundTap();
                Player->SpawnJudgeEffect(extra, JudgeType::SlideTap);
            } else {
                IncrementCombo(extra, judgeTime);
                Player->PlaySoundTap();
                Player->SpawnJudgeEffect(extra, JudgeType::SlideTap);
                note->OnTheFlyData.set((size_t)NoteAttribute::Completed);
            }
        } else {
            if (extra->Type[(size_t)SusNoteType::End] && release) {
                IncrementCombo(extra, judgeTime);
                Player->PlaySoundTap();
                Player->SpawnJudgeEffect(extra, JudgeType::SlideTap);
                note->OnTheFlyData.set((size_t)NoteAttribute::Completed);
            }
        }
        return held;
    }
    return held;
}

bool PlayableProcessor::CheckSlideJudgement(shared_ptr<SusDrawableNoteData> note)
{
    double reltime = Player->CurrentTime - note->StartTime - judgeAdjustSlider;
    if (reltime < -judgeWidthAttack) return false;
    if (reltime >= note->Duration + judgeWidthAttack) return false;
    if (note->OnTheFlyData[(size_t)NoteAttribute::Completed]) return false;

    //���݂̔���ʒu�𒲂ׂ�
    auto lastStep = note;
    auto refNote = note;
    int left = 0, right = 0;
    for (const auto &extra : note->ExtraData) {
        if (extra->Type[(size_t)SusNoteType::Control]) continue;
        if (extra->Type[(size_t)SusNoteType::Injection]) continue;
        if (Player->CurrentTime <= extra->StartTime) {
            refNote = extra;
            break;
        }
        lastStep = refNote = extra;
    }
    if (lastStep == refNote) {
        // �I�_����
        left = lastStep->StartLane;
        right = left + lastStep->Length;
    } else if (reltime < 0) {
        // �n�_���O
        left = note->StartLane;
        right = left + note->Length;
    } else {
        // �J�[�u�f�[�^���ݔ͈͓�
        auto &refcurve = Player->curveData[refNote];
        double timeInBlock = Player->CurrentTime - lastStep->StartTime;
        auto start = refcurve[0];
        auto next = refcurve[0];
        for (const auto &segment : refcurve) {
            if (get<0>(segment) >= timeInBlock) {
                next = segment;
                break;
            }
            start = next = segment;
        }
        auto center = lerp((timeInBlock - get<0>(start)) / (get<0>(next) - get<0>(start)), get<1>(start), get<1>(next)) * 16;
        auto width = lerp(timeInBlock / (refNote->StartTime - lastStep->StartTime), lastStep->Length, refNote->Length);
        left = floor(center - width / 2.0);
        right = ceil(center + width / 2.0);
    }
    // left <= i < right �Ŕ���
    bool held = false, trigger = false, release = false;
    for (int i = note->StartLane; i < (note->StartLane + note->Length); i++) {
        held |= CurrentState->GetCurrentState(ControllerSource::IntegratedSliders, i);
        trigger |= CurrentState->GetTriggerState(ControllerSource::IntegratedSliders, i);
        release |= CurrentState->GetLastState(ControllerSource::IntegratedSliders, i) && !CurrentState->GetCurrentState(ControllerSource::IntegratedSliders, i);
    }
    double judgeTime = Player->CurrentTime - note->StartTime - judgeAdjustSlider;

    // Start����
    if (!note->OnTheFlyData[(size_t)NoteAttribute::Finished]) {
        if (reltime >= judgeWidthAttack) {
            ResetCombo(note);
        } else if (trigger) {
            IncrementCombo(note, judgeTime);
            Player->PlaySoundTap();
            Player->SpawnJudgeEffect(note, JudgeType::ShortNormal);
        }
        return held;
    }

    // Step~End����
    for (const auto &extra : note->ExtraData) {
        judgeTime = Player->CurrentSoundTime - extra->StartTime - judgeAdjustSlider;
        if (extra->OnTheFlyData[(size_t)NoteAttribute::Finished]) continue;
        if (extra->Type[(size_t)SusNoteType::Control]) continue;
        if (extra->Type[(size_t)SusNoteType::Invisible]) continue;

        if (judgeTime < -judgeWidthAttack) {
            return held;
        } else if (judgeTime >= judgeWidthAttack) {
            ResetCombo(extra);
            return held;
        } else if (judgeTime >= 0 && held) {
            if (extra->Type[(size_t)SusNoteType::Injection]) {
                IncrementCombo(extra, judgeTime);
            } else if (extra->Type[(size_t)SusNoteType::Step]) {
                IncrementCombo(extra, judgeTime);
                Player->PlaySoundTap();
                Player->SpawnJudgeEffect(extra, JudgeType::SlideTap);
            } else {
                IncrementCombo(extra, judgeTime);
                Player->PlaySoundTap();
                Player->SpawnJudgeEffect(extra, JudgeType::SlideTap);
                note->OnTheFlyData.set((size_t)NoteAttribute::Completed);
            }
        } else {
            if (extra->Type[(size_t)SusNoteType::End] && release) {
                IncrementCombo(extra, judgeTime);
                Player->PlaySoundTap();
                Player->SpawnJudgeEffect(extra, JudgeType::SlideTap);
                note->OnTheFlyData.set((size_t)NoteAttribute::Completed);
            }
        }
        return held;
    }
    return held;
}

bool PlayableProcessor::CheckAirActionJudgement(shared_ptr<SusDrawableNoteData> note)
{
    double reltime = Player->CurrentTime - note->StartTime - judgeAdjustAirString;
    if (note->OnTheFlyData[(size_t)NoteAttribute::Completed]) return false;

    bool judged = CurrentState->GetCurrentState(ControllerSource::IntegratedAir, (int)AirControlSource::AirHold);
    for (const auto &extra : note->ExtraData) {
        reltime = Player->CurrentSoundTime - extra->StartTime - judgeAdjustAirString;
        if (extra->OnTheFlyData[(size_t)NoteAttribute::Finished]) continue;
        if (extra->Type[(size_t)SusNoteType::Control]) continue;
        if (extra->Type[(size_t)SusNoteType::Invisible]) continue;
        if (extra->Type[(size_t)SusNoteType::Injection] && reltime >= 0) {
            if (isAutoAir || judged) {
                IncrementCombo(extra, reltime);
                return true;
            }
        }
        if (reltime < -judgeWidthAttack) return false;
        if (reltime >= judgeWidthAttack) {
            ResetCombo(extra);
            return false;
        }
        bool aajudged = CurrentState->GetTriggerState(ControllerSource::IntegratedAir, (int)AirControlSource::AirAction);

        if ((isAutoAir && reltime < 0) && !aajudged) continue;
        IncrementCombo(extra, reltime);
        Player->PlaySoundAirAction();
        Player->SpawnJudgeEffect(extra, JudgeType::Action);
        if (extra->Type[(size_t)SusNoteType::End]) note->OnTheFlyData.set((size_t)NoteAttribute::Completed);
        return true;
    }
    return false;
}

void PlayableProcessor::IncrementCombo(shared_ptr<SusDrawableNoteData> note, double reltime)
{
    reltime = fabs(reltime);
    if (reltime <= judgeWidthJusticeCritical) {
        note->OnTheFlyData.set((size_t)NoteAttribute::Finished);
        Status.JusticeCritical++;
        Status.Combo++;
        Status.CurrentGauge += Status.GaugeDefaultMax / Status.AllNotes;
        WriteDebugConsole("JC\n");
    } else if (reltime <= judgeWidthJustice) {
        note->OnTheFlyData.set((size_t)NoteAttribute::Finished);
        Status.Justice++;
        Status.Combo++;
        Status.CurrentGauge += (Status.GaugeDefaultMax / Status.AllNotes) / 1.01;
        WriteDebugConsole("J\n");
    } else {
        note->OnTheFlyData.set((size_t)NoteAttribute::Finished);
        Status.Attack++;
        Status.Combo++;
        Status.CurrentGauge += (Status.GaugeDefaultMax / Status.AllNotes) / 1.01 * 0.5;
        WriteDebugConsole("A\n");
    }
}

void PlayableProcessor::IncrementComboEx(std::shared_ptr<SusDrawableNoteData> note)
{
    note->OnTheFlyData.set((size_t)NoteAttribute::Finished);
    Status.JusticeCritical++;
    Status.Combo++;
    Status.CurrentGauge += Status.GaugeDefaultMax / Status.AllNotes;
}

void PlayableProcessor::IncrementComboHell(std::shared_ptr<SusDrawableNoteData> note, int state)
{
    switch (state) {
        case -1:
            // ��������I���A�����S�z�Ȃ�
            note->OnTheFlyData.reset((size_t)NoteAttribute::HellChecking);
            note->OnTheFlyData.set((size_t)NoteAttribute::Finished);
            break;
        case 0:
            // �Ƃ肠�����ʉ߂����̂�JC
            note->OnTheFlyData.set((size_t)NoteAttribute::HellChecking);
            Status.JusticeCritical++;
            Status.Combo++;
            Status.CurrentGauge += Status.GaugeDefaultMax / Status.AllNotes;
            break;
        case 1:
            // ���ʂɔ��莸�s
            Status.Miss++;
            Status.Combo = 0;
            Status.CurrentGauge -= Status.GaugeDefaultMax / Status.AllNotes * 2;
            note->OnTheFlyData.set((size_t)NoteAttribute::Finished);
            break;
        case 2:
            // �ォ������Ĕ��莸�s
            Status.Miss++;
            Status.JusticeCritical--;
            Status.Combo = 0;
            Status.CurrentGauge -= Status.GaugeDefaultMax / Status.AllNotes * 2;
            note->OnTheFlyData.set((size_t)NoteAttribute::Finished);
    }
}

void PlayableProcessor::IncrementComboAir(std::shared_ptr<SusDrawableNoteData> note, double reltime)
{
    reltime = fabs(reltime);
    if (reltime <= judgeWidthJusticeCritical) {
        note->OnTheFlyData.set((size_t)NoteAttribute::Finished);
        Status.JusticeCritical++;
        Status.Combo++;
        Status.CurrentGauge += Status.GaugeDefaultMax / Status.AllNotes;
    } else if (reltime <= judgeWidthJustice) {
        note->OnTheFlyData.set((size_t)NoteAttribute::Finished);
        Status.Justice++;
        Status.Combo++;
        Status.CurrentGauge += (Status.GaugeDefaultMax / Status.AllNotes) / 1.01;
    } else {
        note->OnTheFlyData.set((size_t)NoteAttribute::Finished);
        Status.Attack++;
        Status.Combo++;
        Status.CurrentGauge += (Status.GaugeDefaultMax / Status.AllNotes) / 1.01 * 0.5;
    }
}

void PlayableProcessor::ResetCombo(shared_ptr<SusDrawableNoteData> note)
{
    note->OnTheFlyData.set((size_t)NoteAttribute::Finished);
    Status.Miss++;
    Status.Combo = 0;
}