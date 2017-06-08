#include "ScenePlayer.h"
#include "ScriptSprite.h"
#include "ExecutionManager.h"

using namespace std;

static SSound *soundTap, *soundExTap, *soundFlick;
static SImage *imageTap, *imageExTap, *imageFlick, *imageHold, *imageHoldStrut, *imageSlide, *imageSlideStrut;
static SFont *fontCombo;
static STextSprite *textCombo;

static uint16_t VertexIndices[] = { 0, 1, 3, 3, 1, 2 };
static VERTEX3D Vertices[] = {
    {
        VGet(-400, 0, 2000),
        VGet(0, 1, 0),
        GetColorU8(255, 255, 255, 255),
        GetColorU8(0, 0, 0, 0),
        0.0f, 0.0f,
        0.0f, 0.0f
    },
    {
        VGet(400, 0, 2000),
        VGet(0, 1, 0),
        GetColorU8(255, 255, 255, 255),
        GetColorU8(0, 0, 0, 0),
        1.0f, 0.0f,
        0.0f, 0.0f
    },
    {
        VGet(400, 0, 0),
        VGet(0, 1, 0),
        GetColorU8(255, 255, 255, 255),
        GetColorU8(0, 0, 0, 0),
        1.0f, 1.0f,
        0.0f, 0.0f
    },
    {
        VGet(-400, 0, 0),
        VGet(0, 1, 0),
        GetColorU8(255, 255, 255, 255),
        GetColorU8(0, 0, 0, 0),
        0.0f, 1.0f,
        0.0f, 0.0f
    }
};

void RegisterPlayerScene(ExecutionManager * manager)
{
    auto engine = manager->GetScriptInterfaceUnsafe()->GetEngine();

    engine->RegisterObjectType(SU_IF_SCENE_PLAYER, 0, asOBJ_REF);
    engine->RegisterObjectBehaviour(SU_IF_SCENE_PLAYER, asBEHAVE_ADDREF, "void f()", asMETHOD(ScenePlayer, AddRef), asCALL_THISCALL);
    engine->RegisterObjectBehaviour(SU_IF_SCENE_PLAYER, asBEHAVE_RELEASE, "void f()", asMETHOD(ScenePlayer, Release), asCALL_THISCALL);
    engine->RegisterObjectMethod(SU_IF_SCENE_PLAYER, "void Initialize()", asMETHOD(ScenePlayer, Initialize), asCALL_THISCALL);
    engine->RegisterObjectMethod(SU_IF_SCENE_PLAYER, "void SetResource(const string &in, " SU_IF_IMAGE "@)", asMETHOD(ScenePlayer, SetPlayerResource), asCALL_THISCALL);
    engine->RegisterObjectMethod(SU_IF_SCENE_PLAYER, "void SetResource(const string &in, " SU_IF_FONT "@)", asMETHOD(ScenePlayer, SetPlayerResource), asCALL_THISCALL);
    engine->RegisterObjectMethod(SU_IF_SCENE_PLAYER, "void SetResource(const string &in, " SU_IF_SOUND "@)", asMETHOD(ScenePlayer, SetPlayerResource), asCALL_THISCALL);
    engine->RegisterObjectMethod(SU_IF_SCENE_PLAYER, "void DrawLanes()", asMETHOD(ScenePlayer, Draw), asCALL_THISCALL);
    engine->RegisterObjectMethod(SU_IF_SCENE_PLAYER, "void Play()", asMETHOD(ScenePlayer, Play), asCALL_THISCALL);
    engine->RegisterObjectMethod(SU_IF_SCENE_PLAYER, "double GetCurrentTime()", asMETHOD(ScenePlayer, GetPlayingTime), asCALL_THISCALL);
    engine->RegisterObjectMethod(SU_IF_SCENE_PLAYER, "int GetSeenObjectsCount()", asMETHOD(ScenePlayer, GetSeenObjectsCount), asCALL_THISCALL);
}


ScenePlayer::ScenePlayer(ExecutionManager *exm) : manager(exm)
{

}

void ScenePlayer::AddRef()
{
    reference++;
}

void ScenePlayer::Release()
{
    if (--reference == 0) {
        Finalize();
        delete this;
    }
}

void ScenePlayer::Finalize()
{
    for (auto& res : resources) res.second->Release();
    manager->GetSoundManagerUnsafe()->Stop(bgmStream);
    manager->GetSoundManagerUnsafe()->ReleaseSound(bgmStream);

    fontCombo->Release();
    DeleteGraph(hGroundBuffer);
    DeleteGraph(hAirBuffer);
}

void ScenePlayer::Draw()
{
    double time = GetPlayingTime() - analyzer->SharedMetaData.WaveOffset;
    double duration = 0.750;
    double preced = 0.1;    //�@�����u�ԂȂǂ̏����̂��߂ɑ�����镪 ����͏��time�
    int division = 8;

    CalculateNotes(time, duration, preced);
    ProcessScore(time, duration, preced);
    ostringstream combo;
    combo << setw(4) << comboCount;
    textCombo->set_Text(combo.str());

    BEGIN_DRAW_TRANSACTION(hGroundBuffer);
    ClearDrawScreen();
    SetDrawBlendMode(DX_BLENDMODE_ALPHA, 255);

    DrawGraph(0, 0, resources["LaneGround"]->GetHandle(), TRUE);
    for (int i = 1; i < division; i++) DrawLineAA(1024 / division * i, 0, 1024 / division * i, 2048, GetColor(255, 255, 255), 2);
    textCombo->Draw();

    DrawShortNotes(time, duration, preced);
    DrawLongNotes(time, duration, preced);
    FINISH_DRAW_TRANSACTION;

    SetUseLighting(FALSE);
    SetCameraPositionAndTarget_UpVecY(VGet(0, 500, -400), VGet(0, 0, 600));
    DrawPolygonIndexed3D(Vertices, 4, VertexIndices, 2, hGroundBuffer, TRUE);
}

void ScenePlayer::CalculateNotes(double time, double duration, double preced)
{
    seenData.clear();
    copy_if(data.begin(), data.end(), back_inserter(seenData), [&](shared_ptr<SusDrawableNoteData> n) {
        double ptime = time - preced;
        if (n->Type.to_ulong() & 0b0000000011100000) {
            // �����O
            return (ptime <= n->StartTime && n->StartTime <= time + duration)
                || (ptime <= n->StartTime + n->Duration && n->StartTime + n->Duration <= time + duration)
                || (n->StartTime <= ptime && time + duration <= n->StartTime + n->Duration);
        } else {
            // �V���[�g
            return (ptime <= n->StartTime && n->StartTime <= time + duration);
        }
    });
}

void ScenePlayer::DrawShortNotes(double time, double duration, double preced)
{
    SetDrawBlendMode(DX_BLENDMODE_ALPHA, 255);
    for (auto& note : seenData) {
        double relpos = 1.0 - (note->StartTime - time) / duration;
        auto length = note->Length;
        auto slane = note->StartLane;
        int handleToDraw = 0;

        if (note->Type.test(SusNoteType::Tap)) {
            handleToDraw = imageTap->GetHandle();
        } else if (note->Type.test(SusNoteType::ExTap)) {
            handleToDraw = imageExTap->GetHandle();
        } else if (note->Type.test(SusNoteType::Flick)) {
            handleToDraw = imageFlick->GetHandle();
        } else {
            //�b��I��
            continue;
        }

        //64*3 x 64 ��`�悷�邩��1/2�ł��K�v������
        for (int i = 0; i < length * 2; i++) {
            int type = i ? (i == length * 2 - 1 ? 2 : 1) : 0;
            DrawRectRotaGraph3F((slane * 2 + i) * 32.0f, 2048.0f * relpos, 64 * type, (0), 64, 64, 0, 32.0f, 0.5f, 0.5f, 0, handleToDraw, TRUE, FALSE);
        }
    }
}

void ScenePlayer::DrawLongNotes(double time, double duration, double preced)
{
    SetDrawBlendMode(DX_BLENDMODE_ALPHA, 255);
    for (auto& note : seenData) {
        double relpos = 1.0 - (note->StartTime - time) / duration;
        auto length = note->Length;
        auto slane = note->StartLane;

        if (note->Type.test(SusNoteType::Hold)) {
            double relendpos = 1.0 - (note->ExtraData[0]->StartTime - time) / duration;
            DrawModiGraphF(
                slane * 64.0f, 2048.0f * relpos,
                (slane + length) * 64.0f, 2048.0f * relpos,
                (slane + length) * 64.0f, 2048.0f * relendpos,
                slane * 64.0f, 2048.0f * relendpos,
                imageHoldStrut->GetHandle(), TRUE
            );
            for (int i = 0; i < length * 2; i++) {
                int type = i ? (i == length * 2 - 1 ? 2 : 1) : 0;
                DrawRectRotaGraph3F((slane * 2 + i) * 32.0f, 2048.0f * relpos, 64 * type, (0), 64, 64, 0, 32.0f, 0.5f, 0.5f, 0, imageHold->GetHandle(), TRUE, FALSE);
                DrawRectRotaGraph3F((slane * 2 + i) * 32.0f, 2048.0f * relendpos, 64 * type, (0), 64, 64, 0, 32.0f, 0.5f, 0.5f, 0, imageHold->GetHandle(), TRUE, FALSE);
            }
        } else if (note->Type.test(SusNoteType::Slide)) {
            DrawSlideNotes(time, duration, note);
        } else if (note->Type.test(SusNoteType::AirAction)) {

        } else {
            continue;
        }
    }
}

void ScenePlayer::DrawSlideNotes(double time, double duration, shared_ptr<SusDrawableNoteData> note)
{
    auto lastStep = note;
    auto lastStepRelativeY = 1.0 - (lastStep->StartTime - time) / duration;
    double segmentLength = 128.0;   // Buffer��ł̍ŏ��̒���
    vector<tuple<double, double>> controlPoints;    // lastStep����̎���, X�����ʒu(0~1)
    vector<tuple<double, double>> segmentPositions;
    vector<tuple<double, double>> bezierBuffer;

    //���݂̕`��͐���_�l���Ȃ�
    for (int i = 0; i < lastStep->Length * 2; i++) {
        int type = i ? (i == lastStep->Length * 2 - 1 ? 2 : 1) : 0;
        DrawRectRotaGraph3F((lastStep->StartLane * 2 + i) * 32.0f, 2048.0f * lastStepRelativeY, 64 * type, (0), 64, 64, 0, 32.0f, 0.5f, 0.5f, 0, imageSlide->GetHandle(), TRUE, FALSE);
    }

    controlPoints.push_back(make_tuple(0, (lastStep->StartLane + lastStep->Length / 2.0) / 16.0));
    for (auto &slideElement : note->ExtraData) {
        if (slideElement->Type.test(SusNoteType::Control)) {
            auto cpi = make_tuple(slideElement->StartTime - lastStep->StartTime, (slideElement->StartLane + slideElement->Length / 2.0) / 16.0);
            controlPoints.push_back(cpi);
            continue;
        }
        // End��Step
        controlPoints.push_back(make_tuple(slideElement->StartTime - lastStep->StartTime, (slideElement->StartLane + slideElement->Length / 2.0) / 16.0));
        double currentStepRelativeY = 1.0 - (slideElement->StartTime - time) / duration;
        int segmentPoints = 2048.0 * (lastStepRelativeY - currentStepRelativeY) / segmentLength + 2;
        segmentPositions.clear();
        for (int j = 0; j < segmentPoints; j++) {
            double relativeTimeInBlock = j / (double)(segmentPoints - 1);
            bezierBuffer.clear();
            copy(controlPoints.begin(), controlPoints.end(), back_inserter(bezierBuffer));
            for (int k = controlPoints.size() - 1; k >= 0; k--) {
                for (int l = 0; l < k; l++) {
                    auto derivedTime = (1.0 - relativeTimeInBlock) * get<0>(bezierBuffer[l]) + relativeTimeInBlock * get<0>(bezierBuffer[l + 1]);
                    auto derivedPosition = (1.0 - relativeTimeInBlock) * get<1>(bezierBuffer[l]) + relativeTimeInBlock * get<1>(bezierBuffer[l + 1]);
                    bezierBuffer[l] = make_tuple(derivedTime, derivedPosition);
                }
            }
            segmentPositions.push_back(bezierBuffer[0]);
        }
        
        auto lastSegmentPosition = segmentPositions[0];
        double lastSegmentLength = lastStep->Length;
        double lastTimeInBlock = get<0>(lastSegmentPosition) / (slideElement->StartTime - lastStep->StartTime);
        auto lastSegmentRelativeY = 1.0 - (lastStep->StartTime - time) / duration;
        for (auto &segmentPosition : segmentPositions) {
            if (lastSegmentPosition == segmentPosition) continue;
            double currentTimeInBlock = get<0>(segmentPosition) / (slideElement->StartTime - lastStep->StartTime);
            double currentSegmentLength = (1.0 - currentTimeInBlock) * lastStep->Length + currentTimeInBlock * slideElement->Length;
            double currentSegmentRelativeY = 1.0 - (lastStep->StartTime + get<0>(segmentPosition) - time) / duration;
            
            SetDrawBlendMode(DX_BLENDMODE_ADD, 255);
            DrawRectModiGraphF(
                get<1>(lastSegmentPosition) * 1024.0 - lastSegmentLength / 2 * 64.0, 2048.0f * lastSegmentRelativeY,
                get<1>(lastSegmentPosition) * 1024.0 + lastSegmentLength / 2 * 64.0, 2048.0f * lastSegmentRelativeY,
                get<1>(segmentPosition) * 1024.0 + currentSegmentLength / 2 * 64.0, 2048.0f * currentSegmentRelativeY,
                get<1>(segmentPosition) * 1024.0 - currentSegmentLength / 2 * 64.0, 2048.0f * currentSegmentRelativeY,
                0, 192.0 * lastTimeInBlock, 64, 192.0 * (currentTimeInBlock - lastTimeInBlock),
                imageSlideStrut->GetHandle(), TRUE
            );
            SetDrawBlendMode(DX_BLENDMODE_ALPHA, 255);
            DrawLineAA(
                get<1>(lastSegmentPosition) * 1024.0, 2048.0f * lastSegmentRelativeY,
                get<1>(segmentPosition) * 1024.0, 2048.0f * currentSegmentRelativeY,
                GetColor(0, 200, 255), 16);

            lastSegmentPosition = segmentPosition;
            lastSegmentLength = currentSegmentLength;
            lastSegmentRelativeY = currentSegmentRelativeY;
            lastTimeInBlock = currentTimeInBlock;
        }
        SetDrawBlendMode(DX_BLENDMODE_ALPHA, 255);
        for (int i = 0; i < slideElement->Length * 2; i++) {
            int type = i ? (i == slideElement->Length * 2 - 1 ? 2 : 1) : 0;
            DrawRectRotaGraph3F((slideElement->StartLane * 2 + i) * 32.0f, 2048.0f * currentStepRelativeY, 64 * type, (0), 64, 64, 0, 32.0f, 0.5f, 0.5f, 0, imageSlide->GetHandle(), TRUE, FALSE);
        }
        lastStep = slideElement;
        lastStepRelativeY = currentStepRelativeY;
        controlPoints.clear();
        controlPoints.push_back(make_tuple(0, (slideElement->StartLane + slideElement->Length / 2.0) / 16.0));
    }
}

void ScenePlayer::ProcessSound(double time, double duration, double preced)
{
    //��ڕs��
}

void ScenePlayer::ProcessScore(double time, double duration, double preced)
{
    for (auto& note : seenData) {
        double relpos = (note->StartTime - time) / duration;
        if (relpos >= 0 || (note->OnTheFlyData.test(NoteAttribute::Finished) && note->ExtraData.size() == 0)) continue;

        if (note->Type.test(SusNoteType::Flick)) {
            soundFlick->Play();
            comboCount++;
            note->OnTheFlyData.set(NoteAttribute::Finished);
        } else if (note->Type.test(SusNoteType::ExTap)) {
            soundExTap->Play();
            comboCount++;
            note->OnTheFlyData.set(NoteAttribute::Finished);
        } else if (note->Type.test(SusNoteType::Tap)
            || note->Type.test(SusNoteType::Air)) {
            soundTap->Play();
            comboCount++;
            note->OnTheFlyData.set(NoteAttribute::Finished);
        } else if (note->Type.test(SusNoteType::Start)) {
            if (!note->OnTheFlyData.test(NoteAttribute::Finished)) {
                soundTap->Play();
                comboCount++;
                note->OnTheFlyData.set(NoteAttribute::Finished);
            }
            for (auto& ex : note->ExtraData) {
                relpos = (ex->StartTime - time) / duration;
                if (relpos < 0 && !ex->OnTheFlyData.test(NoteAttribute::Finished)) {
                    soundTap->Play();
                    comboCount++;
                    ex->OnTheFlyData.set(NoteAttribute::Finished);
                }
            }
        }

    }
}

// �X�N���v�g������Ăׂ���

void ScenePlayer::Initialize()
{
    analyzer = make_unique<SusAnalyzer>(192);
    metaInfo = manager->GetMusicsManager()->Selected;

    hGroundBuffer = MakeScreen(1024, 2048, TRUE);
    hAirBuffer = MakeScreen(1024, 2048, TRUE);

    analyzer->LoadFromFile(metaInfo->Path.string().c_str());
    analyzer->RenderScoreData(data);

    auto file = metaInfo->WavePath;
    bgmStream = manager->GetSoundManagerUnsafe()->LoadStreamFromFile(file.string().c_str());
}

void ScenePlayer::SetPlayerResource(const string & name, SResource * resource)
{
    if (resources.find(name) != resources.end()) resources[name]->Release();
    resources[name] = resource;
}


void ScenePlayer::Play()
{
    imageTap = dynamic_cast<SImage*>(resources["Tap"]);
    imageExTap = dynamic_cast<SImage*>(resources["ExTap"]);
    imageFlick = dynamic_cast<SImage*>(resources["Flick"]);
    imageHold = dynamic_cast<SImage*>(resources["Hold"]);
    imageHoldStrut = dynamic_cast<SImage*>(resources["HoldStrut"]);
    imageSlide = dynamic_cast<SImage*>(resources["Slide"]);
    imageSlideStrut = dynamic_cast<SImage*>(resources["SlideStrut"]);

    soundTap = dynamic_cast<SSound*>(resources["SoundTap"]);
    soundExTap = dynamic_cast<SSound*>(resources["SoundExTap"]);
    soundFlick = dynamic_cast<SSound*>(resources["SoundFlick"]);
    fontCombo = dynamic_cast<SFont*>(resources["FontCombo"]);

    fontCombo->AddRef();
    textCombo = STextSprite::Factory(fontCombo, "0000");
    textCombo->Apply("y:1536, scaleX:8, scaleY:8");

    manager->GetSoundManagerUnsafe()->Play(bgmStream);
}

double ScenePlayer::GetPlayingTime()
{
    return manager->GetSoundManagerUnsafe()->GetPosition(bgmStream);
}

int ScenePlayer::GetSeenObjectsCount()
{
    return seenObjects;
}