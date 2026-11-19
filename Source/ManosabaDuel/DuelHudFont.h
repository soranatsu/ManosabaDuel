#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "DuelHudFont.generated.h"

UCLASS(BlueprintType)
class MANOSABADUEL_API UDuelHudFont final : public UAssetUserData
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category = "Glyphs") int32 Columns = 0;
    UPROPERTY(EditAnywhere, Category = "Glyphs") int32 Padding = 0;
    UPROPERTY(EditAnywhere, Category = "Glyphs") int32 PixelSize = 0;
    UPROPERTY(EditAnywhere, Category = "Glyphs") int32 CellWidth = 0;
    UPROPERTY(EditAnywhere, Category = "Glyphs") int32 CellHeight = 0;
    UPROPERTY(EditAnywhere, Category = "Glyphs") int32 DisplayStart = 0;
    UPROPERTY(EditAnywhere, Category = "Glyphs") int32 CjkStart = 0;
    UPROPERTY(EditAnywhere, Category = "Glyphs") TArray<int32> Codepoints;
    UPROPERTY(EditAnywhere, Category = "Glyphs") TArray<uint8> Advances;
};
