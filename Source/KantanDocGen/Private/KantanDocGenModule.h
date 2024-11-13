// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// Copyright (C) 2016-2017 Cameron Angus. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "DocGenTaskProcessor.h"	// TUniquePtr seems to need full definition...


class FUICommandList;
class FWorkflowAllowedTabSet;
class FBlueprintEditor;

struct FKantanDocGenSettings;

/*
Module implementation
*/
class FKantanDocGenModule : public FDefaultGameModuleImpl
{
public:
	FKantanDocGenModule()
	{}

public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

public:
	void GenerateDocs(const FKantanDocGenSettings& Settings, EKantanDocGenerationMode Mode);
	static void OpenURL(const FKantanDocGenSettings& Settings, bool IsFile = true);
	void OpenClassURL(const FKantanDocGenSettings& Settings, UClass* Class, bool IsFile = true);
	void OpenDefaultURL();
	void OpenDefaultClassURL(UClass* Class);
	;

protected:
	void RegisterOpenDocumentation();
	TSharedRef<FExtender> AddAssetEditorMenuExtender(
		const TSharedRef<FUICommandList> CommandList,
		const TArray<UObject*> EditingObjects) const;
	void AddAssetEditorToolbarExtension(FToolBarBuilder& ToolbarBuilder, UObject* PrimaryObject);

	void ProcessIntermediateDocs(FString const& IntermediateDir, FString const& OutputDir, FString const& DocTitle, bool bCleanOutput);
	void ShowDocGenUI();
	FKantanDocGenSettings CreateDefaultSettings(bool* OutIsFile = nullptr);

protected:
	TUniquePtr< FDocGenTaskProcessor > Processor;
	TSharedPtr< FUICommandList > UICommands;
};


