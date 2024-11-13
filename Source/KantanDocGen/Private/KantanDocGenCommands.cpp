// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// Copyright (C) 2016-2017 Cameron Angus. All Rights Reserved.

#include "KantanDocGenCommands.h"

#include "Modules/ModuleManager.h"
#include "DocGenSettings.h"
#include "KantanDocGenModule.h"

#define LOCTEXT_NAMESPACE "KantanDocGen"


void FKantanDocGenCommands::RegisterCommands()
{
	UI_COMMAND(ShowDocGenUI, "Kantan DocGen", "Shows the Kantan Doc Gen tab", EUserInterfaceActionType::Button, FInputGesture());
	NameToCommandMap.Add(TEXT("ShowDocGenUI"), ShowDocGenUI);
}

static bool KantanDocGenExec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
{
	if (!FParse::Param(Cmd, TEXT("KantanDocGen")))
	{
		return false;
	}

	FKantanDocGenSettings Settings = UKantanDocGenSettingsBase::Get<UKantanDocGenExecCmdSettings>()->Settings;
	FString NewOutput;
	if (FParse::Value(Cmd, TEXT("-Output="), NewOutput))
	{
		Settings.OutputDirectory = FDirectoryPath(NewOutput);
	}


	if (FParse::Param(Cmd, TEXT("Generate")))
	{
		FKantanDocGenModule& Module = FModuleManager::LoadModuleChecked< FKantanDocGenModule >(TEXT("KantanDocGen"));
		Module.GenerateDocs(Settings, EKantanDocGenerationMode::ExecCommand);
	}

	if (FParse::Param(Cmd, TEXT("Open")))
	{
		FKantanDocGenModule::OpenURL(Settings);
	}

	if (FParse::Param(Cmd, TEXT("Quit")))
	{
		FPlatformMisc::RequestExitWithStatus(true, GIsCriticalError ? -1 : 0);
	}
	return true;
}

FStaticSelfRegisteringExec KantanDocGenExecRegistration(KantanDocGenExec);

#undef LOCTEXT_NAMESPACE


