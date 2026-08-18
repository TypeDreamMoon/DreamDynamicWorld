// Copyright DreamMoon. All Rights Reserved.

#include "Modules/ModuleManager.h"

class FDreamWindNiagaraModule : public IModuleInterface
{
public:
	virtual void StartupModule() override {}
	virtual void ShutdownModule() override {}
};

IMPLEMENT_MODULE(FDreamWindNiagaraModule, DreamWindNiagara)
