// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// Copyright (C) 2016-2017 Cameron Angus. All Rights Reserved.

#include "KantanDocGenModule.h"
#include "KantanDocGenLog.h"
#include "KantanDocGenCommands.h"
#include "DocGenSettings.h"
#include "DocGenTaskProcessor.h"
#include "UI/SKantanDocGenWidget.h"

#include "HAL/IConsoleManager.h"
#include "Interfaces/IMainFrameModule.h"
#include "LevelEditor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/RunnableThread.h"
#include "BlueprintEditorModule.h"
#include "BlueprintEditor.h"
#include "BlueprintEditorContext.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/BlueprintEditorUtils.h"

#define LOCTEXT_NAMESPACE "KantanDocGen"


IMPLEMENT_MODULE(FKantanDocGenModule, KantanDocGen)

DEFINE_LOG_CATEGORY(LogKantanDocGen);


void FKantanDocGenModule::StartupModule()
{
	// Create command list
	UICommands = MakeShared< FUICommandList >();

	FKantanDocGenCommands::Register();

	// Map commands
	FUIAction ShowDocGenUI_UIAction(
		FExecuteAction::CreateRaw(this, &FKantanDocGenModule::ShowDocGenUI),
		FCanExecuteAction::CreateLambda([] { return true; })
	);

	TSharedPtr<FUICommandInfo> CmdInfo = FKantanDocGenCommands::Get().ShowDocGenUI;
	UICommands->MapAction(CmdInfo, ShowDocGenUI_UIAction);

	// Setup menu extension
	TFunction<void(FMenuBuilder&)> AddMenuExtension = [](FMenuBuilder& MenuBuilder)
		{
			MenuBuilder.AddMenuEntry(FKantanDocGenCommands::Get().ShowDocGenUI);
		};

	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	TSharedRef<FExtender> MenuExtender(new FExtender());
	MenuExtender->AddMenuExtension(
		TEXT("FileProject"),
		EExtensionHook::After,
		UICommands.ToSharedRef(),
		FMenuExtensionDelegate::CreateLambda(AddMenuExtension)
	);
	LevelEditorModule.GetMenuExtensibilityManager()->AddExtender(MenuExtender);

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(
		this, &FKantanDocGenModule::RegisterOpenDocumentation));
}

void FKantanDocGenModule::ShutdownModule()
{
	FKantanDocGenCommands::Unregister();

	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
}

void FKantanDocGenModule::RegisterOpenDocumentation()
{
	// Use the current object as the owner of the menus
	// This allows us to remove all our custom menus when the 
	// module is unloaded (see ShutdownModule below)
	FToolMenuOwnerScoped OwnerScoped(this);

	// Extend the "File" section of the main toolbar
	UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu(
		TEXT("LevelEditor.LevelEditorToolBar.User"));
	FToolMenuSection& ToolbarSection = ToolbarMenu->FindOrAddSection("Documentation");

	const FString Title = CreateDefaultSettings().DocumentationTitle;
	const FString ToolBarName = FString::Printf(TEXT("Open %s Documentation"), *Title);
	const FString ToolBarDesc = FString::Printf(TEXT("Open %s Documentation on your browser."), *Title);

	FToolMenuEntry ToolbarEntry = FToolMenuEntry::InitToolBarButton(
		TEXT("DocGen_Documentation"),
		FExecuteAction::CreateRaw(this, &FKantanDocGenModule::OpenDefaultURL),
		FText::FromString(ToolBarName),
		FText::FromString(ToolBarDesc),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Documentation"))
	);
	ToolbarEntry.StyleNameOverride = TEXT("CalloutToolbar");
	ToolbarSection.AddEntry(ToolbarEntry);

	FBlueprintEditorModule& BlueprintEditorModule = FModuleManager::LoadModuleChecked<FBlueprintEditorModule>("Kismet");
	BlueprintEditorModule.GetMenuExtensibilityManager()->GetExtenderDelegates().Add(
		FAssetEditorExtender::CreateRaw(this, &FKantanDocGenModule::AddAssetEditorMenuExtender));
}

TSharedRef<FExtender> FKantanDocGenModule::AddAssetEditorMenuExtender(
	const TSharedRef<FUICommandList> CommandList, const TArray<UObject*> EditingObjects) const
{
	TSharedRef<FExtender> Extender(new FExtender());

	UObject* PrimaryObject = nullptr;
	if (EditingObjects.Num() > 0)
	{
		PrimaryObject = EditingObjects[0];
	}

	if (!PrimaryObject)
	{
		return Extender;
	}

	Extender->AddToolBarExtension(
		TEXT("Asset"),
		EExtensionHook::After,
		CommandList,
		FToolBarExtensionDelegate::CreateRaw(const_cast<FKantanDocGenModule*>(this),
			&FKantanDocGenModule::AddAssetEditorToolbarExtension, PrimaryObject));

	return Extender;
}

void FKantanDocGenModule::AddAssetEditorToolbarExtension(FToolBarBuilder& ToolbarBuilder, UObject* PrimaryObject)
{
	UBlueprint* Blueprint = Cast<UBlueprint>(PrimaryObject);
	UClass* ParentClass = Blueprint ? Blueprint->ParentClass : nullptr;
	while (Cast<UBlueprintGeneratedClass>(ParentClass))
	{
		ParentClass = ParentClass->GetSuperClass();
	}
	if (!ParentClass)
	{
		return;
	}

	const FString Title = CreateDefaultSettings().DocumentationTitle;
	const FString FriendlyParentName = FBlueprintEditorUtils::GetFriendlyClassDisplayName(ParentClass).ToString();
	const FString ButtonName = FString::Printf(TEXT("Open %s Documentation for \"%s\""), *Title, *FriendlyParentName);
	const FString ButtonDesc = FString::Printf(TEXT("Open %s Documentation for \"%s\" on your browser."), *Title , *FriendlyParentName);

	ToolbarBuilder.BeginStyleOverride(TEXT("CalloutToolbar"));
	ToolbarBuilder.AddToolBarButton(
		FExecuteAction::CreateLambda([this, ParentClass]() { OpenDefaultClassURL(ParentClass); }), 
		TEXT("DocGen_Documentation"),
		FText::FromString(ButtonName),
		FText::FromString(ButtonDesc),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Documentation")));
	ToolbarBuilder.EndStyleOverride();
}

// @TODO: Idea was to allow quoted values containing spaces, but this isn't possible since the initial console string has
// already been split by whitespace, ignoring quotes...
inline bool MatchPotentiallyQuoted(const TCHAR* Stream, const TCHAR* Match, FString& Value)
{
	while ((*Stream == ' ') || (*Stream == 9))
		Stream++;

	if (FCString::Strnicmp(Stream, Match, FCString::Strlen(Match)) == 0)
	{
		Stream += FCString::Strlen(Match);

		return FParse::Token(Stream, Value, false);
	}

	return false;
}

void FKantanDocGenModule::GenerateDocs(const FKantanDocGenSettings& Settings, EKantanDocGenerationMode Mode)
{
	if (!Processor.IsValid())
	{
		Processor = MakeUnique< FDocGenTaskProcessor >();
	}

	Processor->QueueTask(Settings, Mode);

	if (!Processor->IsRunning())
	{
		if (Mode == EKantanDocGenerationMode::UI)
		{
			FRunnableThread::Create(Processor.Get(), TEXT("KantanDocGenProcessorThread"), 0, TPri_BelowNormal);
		}
		else if (Mode == EKantanDocGenerationMode::ExecCommand)
		{
			Processor->Init();
			Processor->Run();
			Processor->Stop();
			Processor->Exit();
		}
	}
}

void FKantanDocGenModule::ShowDocGenUI()
{
	const FText WindowTitle = LOCTEXT("DocGenWindowTitle", "Kantan Doc Gen");

	TSharedPtr< SWindow > Window =
		SNew(SWindow)
		.Title(WindowTitle)
		.MinWidth(400.0f)
		.MinHeight(300.0f)
		.MaxHeight(600.0f)
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		.SizingRule(ESizingRule::Autosized)
		;

	TSharedRef< SWidget > DocGenContent = SNew(SKantanDocGenWidget);
	Window->SetContent(DocGenContent);

	IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked< IMainFrameModule >("MainFrame");
	TSharedPtr< SWindow > ParentWindow = MainFrame.GetParentWindow();

	if (ParentWindow.IsValid())
	{
		FSlateApplication::Get().AddModalWindow(Window.ToSharedRef(), ParentWindow.ToSharedRef());

		auto Settings = UKantanDocGenSettingsBase::Get<UKantanDocGenSettingsObject>();
		Settings->SaveConfig();
	}
	else
	{
		FSlateApplication::Get().AddWindow(Window.ToSharedRef());
	}
}

#define  DEBUG_TEMP_NO_EXTERNAL_LINK 1

void FKantanDocGenModule::OpenURL(const FKantanDocGenSettings& Settings, bool IsFile/* = true*/)
{
	// @TODO andre.fonseca 03/06/2024: This is temporary. It allows redirect to a help page while the external documentation is not setup.
	if (!IsFile && DEBUG_TEMP_NO_EXTERNAL_LINK)
	{
		FPlatformProcess::LaunchURL(*Settings.OutputDirectory.Path, nullptr, nullptr);
		return;
	}

	FString HyperlinkTarget = Settings.OutputDirectory.Path / Settings.DocumentationTitle / TEXT("index.html");
	if (IsFile)
	{
		HyperlinkTarget = TEXT("file://") / FPaths::ConvertRelativePathToFull(HyperlinkTarget);
	}

	UE_LOG(LogKantanDocGen, Log, TEXT("Invoking hyperlink [%s]"), *HyperlinkTarget);
	FPlatformProcess::LaunchURL(*HyperlinkTarget, nullptr, nullptr);
}

void FKantanDocGenModule::OpenClassURL(const FKantanDocGenSettings& Settings, UClass* Class, bool IsFile /*= true*/)
{
	// @TODO andre.fonseca 03/06/2024: This is temporary. It allows redirect to a help page while the external documentation is not setup.
	if (!IsFile && DEBUG_TEMP_NO_EXTERNAL_LINK)
	{
		FPlatformProcess::LaunchURL(*Settings.OutputDirectory.Path, nullptr, nullptr);
		return;
	}

	FString HyperlinkTarget = Settings.OutputDirectory.Path / Settings.DocumentationTitle /
		Class->GetName() / Class->GetName() + TEXT(".html");
	if (IsFile)
	{
		HyperlinkTarget = TEXT("file://") / FPaths::ConvertRelativePathToFull(HyperlinkTarget);
	}

	UE_LOG(LogKantanDocGen, Log, TEXT("Invoking hyperlink [%s]"), *HyperlinkTarget);
	FPlatformProcess::LaunchURL(*HyperlinkTarget, nullptr, nullptr);
}

void FKantanDocGenModule::OpenDefaultURL()
{
	bool IsFile = false;
	FKantanDocGenSettings DefaultSettings = CreateDefaultSettings(&IsFile);
	OpenURL(CreateDefaultSettings(), IsFile);
}

void FKantanDocGenModule::OpenDefaultClassURL(UClass* Class)
{
	bool IsFile = false;
	FKantanDocGenSettings DefaultSettings = CreateDefaultSettings(&IsFile);
	OpenClassURL(CreateDefaultSettings(), Class, IsFile);
}

FKantanDocGenSettings FKantanDocGenModule::CreateDefaultSettings(bool* OutIsFile /* = nullptr*/)
{
	const UKantanDocGenSettingsObject* UserSettings = UKantanDocGenSettingsBase::Get<UKantanDocGenSettingsObject>();
	const UKantanDocGenExecCmdSettings* ProjectSettings = UKantanDocGenSettingsBase::Get<UKantanDocGenExecCmdSettings>();

	if (OutIsFile)
	{
		*OutIsFile = UserSettings->OpenDocumentationSource != EDocumentationSource::External;
	}

	switch (UserSettings->OpenDocumentationSource)
	{
	case EDocumentationSource::External:
	{
		FKantanDocGenSettings DummySettings;
		DummySettings.DocumentationTitle = ProjectSettings->Settings.DocumentationTitle;
		DummySettings.OutputDirectory.Path = ProjectSettings->ExternalDocumentationURL;
		return DummySettings;
	}
	case EDocumentationSource::ProjectSettings:
	{
		return ProjectSettings->Settings;
	}
	case EDocumentationSource::UserPreferences:
	{
		return UserSettings->Settings;
	}
	default:
	{
		checkNoEntry();
		return {};
	}
	}
}

#undef LOCTEXT_NAMESPACE


