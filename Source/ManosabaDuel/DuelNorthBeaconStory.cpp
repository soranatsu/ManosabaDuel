#include "DuelNorthProgress.h"

FString FDuelNorthProgress::BeaconEvidenceText(FName EvidenceId) const
{
    if (EvidenceId == TEXT("BeaconChart"))
        return TEXT("值班记录。昨晚扣下艾玛和希罗的原信，另把两封红蜡封口的信送进去。此前也有女孩收信后被带去候审。\n观察记录在信件检查室，寄信人的日记留在看守住处。");
    if (EvidenceId == TEXT("LensWorkshop"))
        return TEXT("看守的观察记录。昨晚，牢里只有收信的女孩一人。她拆信后喊「别推我」，手上长出黑纹。寄信的人站在门外施法。\n看守撞翻了灯，门外的人停下施法，女孩手上的黑纹便淡下去。今晚换了个女孩拆信，又发生了同样的事。");
    if (EvidenceId == TEXT("BeaconLetter"))
        return TEXT("「人类杀光了我的族人。我生来就是魔女，如今只剩我这个最后的大魔女。\n我要让全人类都变成魔女，让他们也尝到被猎杀的滋味。岛上的女孩只是开始，下一批信要送到岛外。\n信会让她们看见最害怕的事，再由我的咒催动变化。今天施法被打断，那女孩手上的黑纹退了；还没定形的变化，必须由我继续维持。\n吩咐看守：求救信扣下，装进红蜡封袋。我的戒指留在物证室。——小雪」");
    if (EvidenceId == TEXT("BeaconSwitch"))
        return HasBeacon(EvidenceId) ? TEXT("红蜡封袋已经打开，里面是女孩们被扣下的求救信。戒指压出的印与两封假信的红蜡印相合，右边一瓣都缺角。带着证据去终审厅。") :
            TEXT("先查值班记录、观察记录和日记，再来开封袋、对红蜡印。");
    return FString();
}
