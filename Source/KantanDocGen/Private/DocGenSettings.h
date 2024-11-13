// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// Copyright (C) 2016-2017 Cameron Angus. All Rights Reserved.

#pragma once

#include "UObject/UnrealType.h"
#include "Engine/EngineTypes.h"
#include "Engine/DeveloperSettingsBackedByCVars.h"
#include "UObject/ObjectMacros.h"
#include "GameFramework/Actor.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "DocGenSettings.generated.h"

UENUM(BlueprintType)
enum class EDocumentationSource : uint8
{
	External,
	ProjectSettings,
	UserPreferences,
};

USTRUCT()
struct FKantanDocGenSettings
{
	GENERATED_BODY()

public:
	// The list of engine modules to document. 
	// Documenting every single module would take way too long.
	// As such, we need to specify which modules to include.
	UPROPERTY(EditAnywhere, Category = "Generation")
	TArray<FName> BaseEngineModulesToInclude =
	{
		TEXT("Engine"),
		TEXT("AIModule"),
		TEXT("UMG")
	};

	// If true, all engine plugins will be documented.
	UPROPERTY(EditAnywhere, Category = "Generation")
	bool IncludeEnginePlugins = true;

	// If true, the project will be documented.
	UPROPERTY(EditAnywhere, Category = "Generation")
	bool IncludeBaseProject = true;

	// If true, all project plugins will be documented.
	UPROPERTY(EditAnywhere, Category = "Generation")
	bool IncludeProjectPlugins = true;

	//UPROPERTY(EditAnywhere, Category = "Documentation")
	FString DocumentationTitle;

	/** List of paths in which to search for blueprints to document. */
	//UPROPERTY(EditAnywhere, Category = "Class Search", Meta = (ContentDir))
	TArray< FDirectoryPath > ContentPaths;

	/** Names of specific classes/blueprints to document. */
	//UPROPERTY()
	TArray< FName > SpecificClasses;

	/** Names of specific classes/blueprints to exclude. */
	//UPROPERTY()
	TArray< FName > ExcludedClasses;

	//UPROPERTY(EditAnywhere, Category = "Output")
	FDirectoryPath OutputDirectory;

	//UPROPERTY(EditAnywhere, Category = "Class Search", AdvancedDisplay)
	TSubclassOf< UObject > BlueprintContextClass;

	//UPROPERTY(EditAnywhere, Category = "Output")
	bool bCleanOutputDirectory = true;

public:
	FKantanDocGenSettings()
	{
		BlueprintContextClass = AActor::StaticClass();
		bCleanOutputDirectory = true;
	}

	bool HasAnySources() const
	{
		return BaseEngineModulesToInclude.Num() > 0
			|| IncludeEnginePlugins
			|| IncludeBaseProject
			|| IncludeProjectPlugins
			;
	}
};

UCLASS(Config = Editor, Abstract)
class UKantanDocGenSettingsBase : public UDeveloperSettingsBackedByCVars
{
	GENERATED_BODY()

public:
	template<class TKantanDocGenSettings>
	static TKantanDocGenSettings* Get()
	{
		// This is a singleton, use default object
		TKantanDocGenSettings* DefaultSettings = GetMutableDefault< TKantanDocGenSettings >();

		if (!DefaultSettings->bInitialized)
		{
			InitDefaults(DefaultSettings);
			DefaultSettings->bInitialized = true;
		}

		return DefaultSettings;
	}

	static void InitDefaults(UKantanDocGenSettingsBase* CDO)
	{
		if (CDO->Settings.DocumentationTitle.IsEmpty())
		{
			CDO->Settings.DocumentationTitle = FApp::GetProjectName();
		}

		if (CDO->Settings.OutputDirectory.Path.IsEmpty())
		{
			CDO->Settings.OutputDirectory.Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), CDO->OutputDirectory.Path);
		}

		if (CDO->Settings.BlueprintContextClass == nullptr)
		{
			CDO->Settings.BlueprintContextClass = AActor::StaticClass();
		}
	}


	void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override
	{
		const FName MemberPropertyName = (PropertyChangedEvent.MemberProperty != nullptr) ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;

		if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(UKantanDocGenSettingsBase, OutputDirectory))
		{
			Settings.OutputDirectory.Path = OutputDirectory.Path;
			FPaths::MakePathRelativeTo(OutputDirectory.Path, *FPaths::ProjectDir());
		}

		Super::PostEditChangeProperty(PropertyChangedEvent);
	}

public:
	UPROPERTY(EditAnywhere, Config, Category = "Kantan DocGen", Meta = (ShowOnlyInnerProperties))
	FKantanDocGenSettings Settings;

	UPROPERTY(EditAnywhere, Config, Category = "Kantan DocGen", meta = (AbsolutePath))
	FDirectoryPath OutputDirectory{ TEXT("Saved/KantanDocGen") };

	bool bInitialized = false;
};

// The settings are used when running Kantan Doc Gen through UI.
// Similar to UKantanDocGenExecCmdSettings but stored in Editor Preferences instead of project settings
UCLASS(Config = EditorPerProjectUserSettings, meta = (DisplayName = "Kantan Doc Gen"))
class UKantanDocGenSettingsObject : public UKantanDocGenSettingsBase
{
	GENERATED_BODY()

public:
	/**
	 * When opening documentation, where should the documentation be pulled from:
	 * External - External URL defined in ProjectSettings->KantanDocGen
	 * ProjectSettings - Local Output Directory defined in ProjectSettings->KantanDocGen
	 * UserPreferences - Local Output Directory defined in ProjectPreferences->KantanDocGen
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Kantan DocGen")
	EDocumentationSource OpenDocumentationSource = EDocumentationSource::External;
};

// The settings are used when running Kantan Doc Gen through cmd.
// Similar to UKantanDocGenSettingsObject but stored in project settings instead of Editor Preferences
UCLASS(Config = Editor, meta = (DisplayName = "Kantan Doc Gen"))
class UKantanDocGenExecCmdSettings : public UKantanDocGenSettingsBase
{
	GENERATED_BODY()

public:
	// External URL that contains documentation for the project.
	UPROPERTY(EditAnywhere, Config, Category = "Kantan DocGen")
	FString ExternalDocumentationURL;
};
