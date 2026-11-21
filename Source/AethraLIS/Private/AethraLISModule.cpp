#include "LisTypes.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, AethraLIS);
DEFINE_LOG_CATEGORY(LogAethraLIS);

const TCHAR* LisName(ELisTrait Value)
{
    static const TCHAR* Names[] = { TEXT("Coragem"), TEXT("Ambição"), TEXT("Empatia"), TEXT("Curiosidade"), TEXT("Agressividade"), TEXT("Lealdade"), TEXT("Sociabilidade"), TEXT("Cautela") };
    static_assert(UE_ARRAY_COUNT(Names) == LisNum<ELisTrait>());
    return Names[LisIdx(Value)];
}

const TCHAR* LisName(ELisNeed Value)
{
    static const TCHAR* Names[] = { TEXT("Segurança"), TEXT("Alimentação"), TEXT("Descanso"), TEXT("Trabalho"), TEXT("Convivência"), TEXT("Propósito") };
    static_assert(UE_ARRAY_COUNT(Names) == LisNum<ELisNeed>());
    return Names[LisIdx(Value)];
}

const TCHAR* LisName(ELisPlace Value)
{
    static const TCHAR* Names[] = { TEXT("Vila"), TEXT("Campos"), TEXT("Floresta"), TEXT("Rio"), TEXT("Mercado"), TEXT("Ermo") };
    static_assert(UE_ARRAY_COUNT(Names) == LisNum<ELisPlace>());
    return Names[LisIdx(Value)];
}

const TCHAR* LisName(ELisProfession Value)
{
    static const TCHAR* Names[] = { TEXT("Agricultor"), TEXT("Pescador"), TEXT("Lenhador"), TEXT("Comerciante"), TEXT("Guarda"), TEXT("Explorador") };
    static_assert(UE_ARRAY_COUNT(Names) == LisNum<ELisProfession>());
    return Names[LisIdx(Value)];
}

const TCHAR* LisName(ELisAction Value)
{
    static const TCHAR* Names[] = { TEXT("Ocioso"), TEXT("Comer"), TEXT("Dormir"), TEXT("Plantar/colher"), TEXT("Pescar"), TEXT("Cortar lenha"), TEXT("Vender"), TEXT("Comprar comida"),
        TEXT("Pedir ajuda"), TEXT("Ajudar"), TEXT("Conviver"), TEXT("Explorar"), TEXT("Patrulhar"), TEXT("Viajar") };
    static_assert(UE_ARRAY_COUNT(Names) == LisNum<ELisAction>());
    return Names[LisIdx(Value)];
}

const TCHAR* LisName(ELisGoal Value)
{
    static const TCHAR* Names[] = { TEXT("Sobreviver"), TEXT("Dominar o ofício"), TEXT("Prosperar"), TEXT("Apoiar a comunidade"), TEXT("Descobrir") };
    static_assert(UE_ARRAY_COUNT(Names) == LisNum<ELisGoal>());
    return Names[LisIdx(Value)];
}

const TCHAR* LisName(ELisEvent Value)
{
    static const TCHAR* Names[] = { TEXT("Escassez de alimentos"), TEXT("Recurso descoberto"), TEXT("Recurso esgotado"), TEXT("Recurso removido"), TEXT("Bloco colocado"),
        TEXT("Ajuda dada"), TEXT("Ajuda recusada"), TEXT("Negociação bem-sucedida"), TEXT("Negociação falhou"), TEXT("Conflito"), TEXT("Perigo"), TEXT("Perigo repelido"), TEXT("Objetivo mudou") };
    static_assert(UE_ARRAY_COUNT(Names) == LisNum<ELisEvent>());
    return Names[LisIdx(Value)];
}

ELisPlace LisWorkplace(ELisProfession Profession)
{
    switch (Profession)
    {
    case ELisProfession::Farmer: return ELisPlace::Fields;
    case ELisProfession::Fisher: return ELisPlace::River;
    case ELisProfession::Woodcutter: return ELisPlace::Forest;
    case ELisProfession::Merchant: return ELisPlace::Market;
    case ELisProfession::Explorer: return ELisPlace::Wilds;
    default: return ELisPlace::Village;
    }
}

ELisProfession LisSkillFor(ELisAction Action)
{
    switch (Action)
    {
    case ELisAction::Farm: return ELisProfession::Farmer;
    case ELisAction::Fish: return ELisProfession::Fisher;
    case ELisAction::Chop: return ELisProfession::Woodcutter;
    case ELisAction::Sell:
    case ELisAction::BuyFood: return ELisProfession::Merchant;
    case ELisAction::Patrol: return ELisProfession::Guard;
    case ELisAction::Explore: return ELisProfession::Explorer;
    default: return ELisProfession::Count;
    }
}
