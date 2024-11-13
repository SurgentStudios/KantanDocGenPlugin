// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// Copyright (C) 2016-2017 Cameron Angus. All Rights Reserved.

#pragma once

#include "NodeDocsGenerator.h"
#include "KantanDocGenLog.h"
#include "SGraphNode.h"
#include "SGraphPanel.h"
#include "NodeFactory.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "BlueprintActionDatabase.h"
#include "BlueprintNodeSpawner.h"
#include "BlueprintFunctionNodeSpawner.h"
#include "BlueprintBoundNodeSpawner.h"
#include "BlueprintComponentNodeSpawner.h"
#include "BlueprintEventNodeSpawner.h"
#include "BlueprintBoundEventNodeSpawner.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Message.h"
#include "HighResScreenshot.h"
#include "XmlFile.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "TextureResource.h"
#include "ThreadingHelpers.h"
#include "Stats/StatsMisc.h"
#include "ImageWriteTask.h"
#include "AnimGraphNode_Base.h"
#include "SourceCodeNavigation.h"

FNodeDocsGenerator::~FNodeDocsGenerator()
{
	CleanUp();
}

bool FNodeDocsGenerator::GT_Init(FString const& InDocsTitle, FString const& InOutputDir,
	const TMap<FName, TPair<FString, FString>>& InModulePluginNameAndDesc, UClass* BlueprintContextClass)
{
	ModulePluginNameAndDesc = InModulePluginNameAndDesc;

	DummyBP = CastChecked< UBlueprint >(FKismetEditorUtilities::CreateBlueprint(
		BlueprintContextClass,
		::GetTransientPackage(),
		NAME_None,
		EBlueprintType::BPTYPE_Normal,
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass(),
		NAME_None
	));
	if (!DummyBP.IsValid())
	{
		return false;
	}

	Graph = FBlueprintEditorUtils::CreateNewGraph(DummyBP.Get(), TEXT("TempoGraph"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());

	DummyBP->AddToRoot();
	Graph->AddToRoot();

	GraphPanel = SNew(SGraphPanel)
		.GraphObj(Graph.Get())
		;
	// We want full detail for rendering, passing a super-high zoom value will guarantee the highest LOD.
	GraphPanel->RestoreViewSettings(FVector2D(0, 0), 10.0f);

	DocsTitle = InDocsTitle;

	IndexXml = InitIndexXml(DocsTitle);
	ClassDocsMap.Empty();

	OutputDir = InOutputDir;

	return true;
}

UK2Node* FNodeDocsGenerator::GT_InitializeForSpawner(UBlueprintNodeSpawner* Spawner, UObject* SourceObject, FNodeProcessingState& OutState)
{
	if (!IsSpawnerDocumentable(Spawner, SourceObject->IsA< UBlueprint >()))
	{
		return nullptr;
	}

	// Spawn an instance into the graph
	auto NodeInst = Spawner->Invoke(Graph.Get(), IBlueprintNodeBinder::FBindingSet{}, FVector2D(0, 0));

	// Currently Blueprint nodes only
	auto K2NodeInst = Cast< UK2Node >(NodeInst);

	if (K2NodeInst == nullptr)
	{
		UE_LOG(LogKantanDocGen, Warning, TEXT("Failed to create node from spawner of class %s with node class %s."), *Spawner->GetClass()->GetName(), Spawner->NodeClass ? *Spawner->NodeClass->GetName() : TEXT("None"));
		return nullptr;
	}

	UClass* AssociatedClass = MapToAssociatedClass(K2NodeInst, SourceObject);
	UPackage* Package = AssociatedClass->GetOutermost();
	const FString ModuleName = Package->GetName().Replace(TEXT("/Script/"), TEXT(""));
	const TPair<FString, FString>& PluginNameAndDescription = ModulePluginNameAndDesc.FindChecked(*ModuleName);

	if (!ClassDocsMap.Contains(AssociatedClass))
	{
		// New class xml file needs adding
		ClassDocsMap.Add(AssociatedClass, InitClassDocXml(AssociatedClass, ModuleName));
		// Also update the index xml
		UpdateIndexDocWithClass(IndexXml.Get(), AssociatedClass, ModuleName,
			PluginNameAndDescription.Key, PluginNameAndDescription.Value);
	}

	OutState = FNodeProcessingState();
	OutState.ClassDocXml = ClassDocsMap.FindChecked(AssociatedClass);
	OutState.ClassDocsPath = OutputDir / GetClassDocId(AssociatedClass);

	return K2NodeInst;
}

bool FNodeDocsGenerator::GT_Finalize(FString OutputPath)
{
	for (TPair<TWeakObjectPtr<UClass>, TSharedPtr<FXmlFile>>& ClassDoc : ClassDocsMap)
	{
		FinalizeClassDocXml(ClassDoc.Key.Get(), ClassDoc.Value);
	}

	if (!SaveClassDocXml(OutputPath))
	{
		return false;
	}

	if (!SaveIndexXml(OutputPath))
	{
		return false;
	}

	return true;
}

void FNodeDocsGenerator::CleanUp()
{
	if (GraphPanel.IsValid())
	{
		GraphPanel.Reset();
	}

	if (DummyBP.IsValid())
	{
		DummyBP->RemoveFromRoot();
		DummyBP.Reset();
	}
	if (Graph.IsValid())
	{
		Graph->RemoveFromRoot();
		Graph.Reset();
	}
}

bool FNodeDocsGenerator::GenerateNodeImage(UEdGraphNode* Node, FNodeProcessingState& State)
{
	SCOPE_SECONDS_COUNTER(GenerateNodeImageTime);

	const FVector2D DrawSize(1024.0f, 1024.0f);

	bool bSuccess = false;

	AdjustNodeForSnapshot(Node);

	FString NodeName = GetNodeDocId(Node);

	FIntRect Rect;

	TUniquePtr<TImagePixelData<FLinearColor>> PixelData;

	bSuccess = DocGenThreads::RunOnGameThreadRetVal([this, Node, NodeName, DrawSize, &Rect, &PixelData]
		{
			auto NodeWidget = FNodeFactory::CreateNodeWidget(Node);
			NodeWidget->SetOwner(GraphPanel.ToSharedRef());

			const bool bUseGammaCorrection = true;
			FWidgetRenderer Renderer(bUseGammaCorrection);
			Renderer.SetIsPrepassNeeded(true);
			auto RenderTarget = Renderer.DrawWidget(NodeWidget.ToSharedRef(), DrawSize);

			auto DesiredAsFloat = NodeWidget->GetDesiredSize();
			const FIntPoint Desired(static_cast<int32>(DesiredAsFloat.X), static_cast<int32>(DesiredAsFloat.Y));

			FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
			Rect = FIntRect(0, 0, Desired.X, Desired.Y);
			FReadSurfaceDataFlags ReadPixelFlags(RCM_UNorm);
			ReadPixelFlags.SetLinearToGamma(false);

			PixelData = MakeUnique<TImagePixelData<FLinearColor>>(Desired);
			PixelData->Pixels.SetNumUninitialized(Desired.X * Desired.Y);

			if (RTResource->ReadLinearColorPixelsPtr(PixelData->Pixels.GetData(), ReadPixelFlags, Rect) == false)
			{
				UE_LOG(LogKantanDocGen, Warning, TEXT("Failed to read pixels for node %s image."), *NodeName);
				return false;
			}
			if (!PixelData->IsDataWellFormed())
			{
				UE_LOG(LogKantanDocGen, Warning, TEXT("Data was not well formed for node %s image."), *NodeName);
				return false;
			}

			return true;
		});

	if (!bSuccess)
	{
		return false;
	}

	State.RelImageBasePath = TEXT("../img");
	FString ImageBasePath = State.ClassDocsPath / TEXT("img");// State.RelImageBasePath;
	FString ImgFilename = FString::Printf(TEXT("nd_img_%s.png"), *NodeName);
	FString ScreenshotSaveName = ImageBasePath / ImgFilename;

	TUniquePtr<FImageWriteTask> ImageTask = MakeUnique<FImageWriteTask>();
	ImageTask->PixelData = MoveTemp(PixelData);
	ImageTask->Filename = ScreenshotSaveName;
	ImageTask->Format = EImageFormat::PNG;
	ImageTask->CompressionQuality = (int32)EImageCompressionQuality::Default;
	ImageTask->bOverwriteFile = true;
	ImageTask->PixelPreProcessors.Add(TAsyncAlphaWrite<FLinearColor>(255));

	if (ImageTask->RunTask())
	{
		// Success!
		bSuccess = true;
		State.ImageFilename = ImgFilename;
	}
	else
	{
		UE_LOG(LogKantanDocGen, Warning, TEXT("Failed to save screenshot image for node: %s"), *NodeName);
	}

	return bSuccess;
}

inline FString WrapAsCDATA(FString const& InString)
{
	return TEXT("<![CDATA[") + InString + TEXT("]]>");
}

inline FXmlNode* AppendChild(FXmlNode* Parent, FString const& Name)
{
	Parent->AppendChildNode(Name, FString());
	return Parent->GetChildrenNodes().Last();
}

inline FXmlNode* AppendChildRaw(FXmlNode* Parent, FString const& Name, FString const& TextContent)
{
	Parent->AppendChildNode(Name, TextContent);
	return Parent->GetChildrenNodes().Last();
}

inline FXmlNode* AppendChildCDATA(FXmlNode* Parent, FString const& Name, FString const& TextContent)
{
	Parent->AppendChildNode(Name, WrapAsCDATA(TextContent));
	return Parent->GetChildrenNodes().Last();
}

inline TArray<FXmlNode*> FindChildrenNodes(FXmlNode* Parent, FString const& Name)
{
	TArray<FXmlNode*> AllChildren = Parent->GetChildrenNodes();
	return AllChildren.FilterByPredicate([Name](FXmlNode* Child)
		{ return Child->GetTag() == Name; });
}

inline FXmlNode* FindChildWithGrandchildOfContent(FXmlNode* Parent, FString const& ChildName,
	FString const& GrandchildName, FString const& GrandchildContent)
{
	TArray<FXmlNode*> AllChildren = FindChildrenNodes(Parent, ChildName);
	FXmlNode** FoundGrandchild = AllChildren.FindByPredicate([GrandchildName, GrandchildContent](FXmlNode* Child)
		{
			const FXmlNode* Grandchild = Child->FindChildNode(GrandchildName);
			return Grandchild && Grandchild->GetContent() == WrapAsCDATA(GrandchildContent);
		});

	return FoundGrandchild ? *FoundGrandchild : nullptr;
}

// For K2 pins only!
bool ExtractPinInformation(UEdGraphPin* Pin, FString& OutName, FString& OutType, FString& OutDescription)
{
	FString Tooltip;
	Pin->GetOwningNode()->GetPinHoverText(*Pin, Tooltip);

	if (!Tooltip.IsEmpty())
	{
		// @NOTE: This is based on the formatting in UEdGraphSchema_K2::ConstructBasicPinTooltip.
		// If that is changed, this will fail!

		auto TooltipPtr = *Tooltip;

		// Parse name line
		FParse::Line(&TooltipPtr, OutName);
		// Parse type line
		FParse::Line(&TooltipPtr, OutType);

		// Currently there is an empty line here, but FParse::Line seems to gobble up empty lines as part of the previous call.
		// Anyway, attempting here to deal with this generically in case that weird behaviour changes.
		while (*TooltipPtr == TEXT('\n'))
		{
			FString Buf;
			FParse::Line(&TooltipPtr, Buf);
		}

		// What remains is the description
		OutDescription = TooltipPtr;
	}

	// @NOTE: Currently overwriting the name and type as suspect this is more robust to future engine changes.

	OutName = Pin->GetDisplayName().ToString();
	if (OutName.IsEmpty() && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
	{
		OutName = Pin->Direction == EEdGraphPinDirection::EGPD_Input ? TEXT("In") : TEXT("Out");
	}

	OutType = UEdGraphSchema_K2::TypeToText(Pin->PinType).ToString();

	return true;
}

TSharedPtr< FXmlFile > FNodeDocsGenerator::InitIndexXml(FString const& IndexTitle)
{
	const FString FileTemplate = R"xxx(<?xml version="1.0" encoding="UTF-8"?>
<root></root>)xxx";

	TSharedPtr< FXmlFile > File = MakeShared< FXmlFile >(FileTemplate, EConstructMethod::ConstructFromBuffer);
	auto Root = File->GetRootNode();

	AppendChildCDATA(Root, TEXT("display_name"), IndexTitle);

	return File;
}

TSharedPtr< FXmlFile > FNodeDocsGenerator::InitClassDocXml(UClass* Class, const FString& ModuleName)
{
	const FString FileTemplate = R"xxx(<?xml version="1.0" encoding="UTF-8"?>
<root></root>)xxx";

	TSharedPtr< FXmlFile > File = MakeShared< FXmlFile >(FileTemplate, EConstructMethod::ConstructFromBuffer);
	FXmlNode* Root = File->GetRootNode();

	AppendChildCDATA(Root, TEXT("docs_name"), DocsTitle);
	AppendChildCDATA(Root, TEXT("id"), GetClassDocId(Class));

	const FString ClassDisplayName = FBlueprintEditorUtils::GetFriendlyClassDisplayName(Class).ToString();
	AppendChildCDATA(Root, TEXT("display_name"), FBlueprintEditorUtils::GetFriendlyClassDisplayName(Class).ToString());

	const FString ClassTooltip = Class->GetToolTipText().ToString();
	if (ClassTooltip != ClassDisplayName)
	{
		AppendChildCDATA(Root, TEXT("description"), ClassTooltip);
	}

	FXmlNode* References = AppendChild(Root, TEXT("references"));
	if (!ModuleName.IsEmpty())
	{
		AppendChildCDATA(References, TEXT("module"), ModuleName);
	}

	FString ClassHeaderPath, ClassSourcePath;
	FSourceCodeNavigation::FindClassHeaderPath(Class, ClassHeaderPath);
	FSourceCodeNavigation::FindClassSourcePath(Class, ClassSourcePath);

	if (!ClassHeaderPath.IsEmpty())
	{
		if (FPaths::IsUnderDirectory(ClassHeaderPath, FPaths::EngineDir()))
		{
			FPaths::MakePathRelativeTo(ClassHeaderPath, *FPaths::EngineDir());
			ClassHeaderPath = TEXT("Engine/") + ClassHeaderPath;
			if (!ClassSourcePath.IsEmpty())
			{
				FPaths::MakePathRelativeTo(ClassSourcePath, *FPaths::EngineDir());
			}
			ClassSourcePath = TEXT("Engine/") + ClassSourcePath;
		}
		else
		{
			FPaths::MakePathRelativeTo(ClassHeaderPath, *FPaths::ProjectDir());
			if (!ClassSourcePath.IsEmpty())
			{
				FPaths::MakePathRelativeTo(ClassSourcePath, *FPaths::ProjectDir());
			}
		}

		AppendChildCDATA(References, TEXT("header"), ClassHeaderPath);
		if (!ClassSourcePath.IsEmpty())
		{
			AppendChildCDATA(References, TEXT("source"), ClassSourcePath);
		}
	}

	const FString IncludePath = Class->GetMetaData(TEXT("IncludePath"));
	if (!IncludePath.IsEmpty())
	{
		AppendChildCDATA(References, TEXT("include"), IncludePath);
	}

	AppendChild(Root, TEXT("nodes"));

	return File;
}

void FNodeDocsGenerator::FinalizeClassDocXml(UClass* Class, TSharedPtr<FXmlFile> Doc)
{
	FXmlNode* Root = Doc->GetRootNode();


	// Inheritance and interface documentation needs to be done on finalize because it requires knowing which classes are documented.
	TArray<UClass*> InheritanceHierarchy;
	UClass* Parent = Class->GetSuperClass();
	while (Parent)
	{
		InheritanceHierarchy.Add(Parent);
		Parent = Parent->GetSuperClass();
	}

	Algo::Reverse(InheritanceHierarchy);

	FXmlNode* Inheritance = AppendChild(Root, TEXT("inheritance"));
	for (UClass* SuperClass : InheritanceHierarchy)
	{
		const FString SuperClassId = GetClassDocId(SuperClass);
		FXmlNode* ClassElem = AppendChild(Inheritance, TEXT("superClass"));

		if (ClassDocsMap.Contains(SuperClass))
		{
			AppendChildCDATA(ClassElem, TEXT("id"), SuperClassId);
		}

		const FString SuperClassDisplayName = FBlueprintEditorUtils::GetFriendlyClassDisplayName(SuperClass).ToString();
		AppendChildCDATA(ClassElem, TEXT("display_name"), SuperClassDisplayName);
	}

	if (Class->Interfaces.Num() > 0)
	{
		if (Class->Interfaces.Num() > 1)
		{
			int32 BreakHere = 1;
		}

		FXmlNode* Interfaces = AppendChild(Root, TEXT("interfaces"));
		for (const FImplementedInterface& Interface : Class->Interfaces)
		{
			UClass* InterfaceClass = Interface.Class.Get();
			ensureAlways(InterfaceClass);

			const FString InterfaceId = GetClassDocId(InterfaceClass);
			FXmlNode* InterfaceElement = AppendChild(Interfaces, TEXT("interface"));

			if (ClassDocsMap.Contains(InterfaceClass))
			{
				AppendChildCDATA(InterfaceElement, TEXT("id"), InterfaceId);
			}

			const FString InterfaceDisplayName = FBlueprintEditorUtils::GetFriendlyClassDisplayName(InterfaceClass).ToString();
			AppendChildCDATA(InterfaceElement, TEXT("display_name"), InterfaceDisplayName);
		}
	}

}

bool FNodeDocsGenerator::UpdateIndexDocWithClass(FXmlFile* DocFile, UClass* Class, const FString& ModuleName,
	const FString& PluginName, const FString& PluginDescription)
{
	const FString ClassId = GetClassDocId(Class);
	const FString PluginId = PluginName.Replace(TEXT(" "), TEXT("_"));

	TArray<FXmlNode*> RootChildren = FindChildrenNodes(DocFile->GetRootNode(), TEXT("plugin"));

	FXmlNode* Plugin = FindChildWithGrandchildOfContent(DocFile->GetRootNode(), TEXT("plugin"), TEXT("id"), PluginId);
	if (!Plugin)
	{
		Plugin = AppendChild(DocFile->GetRootNode(), TEXT("plugin"));

		AppendChildCDATA(Plugin, TEXT("id"), PluginId);
		AppendChildCDATA(Plugin, TEXT("display_name"), PluginName);
		AppendChildCDATA(Plugin, TEXT("description"), PluginDescription);
		AppendChild(Plugin, TEXT("modules"));
	}

	FXmlNode* Modules = Plugin->FindChildNode(TEXT("modules"));
	FXmlNode* Module = FindChildWithGrandchildOfContent(Modules, TEXT("module"), TEXT("display_name"), ModuleName);
	if (!Module)
	{
		Module = AppendChild(Modules, TEXT("module"));
		AppendChildCDATA(Module, TEXT("display_name"), ModuleName);
		AppendChild(Module, TEXT("classes"));
	}

	FXmlNode* Classes = Module->FindChildNode(TEXT("classes"));
	FXmlNode* ClassElem = AppendChild(Classes, TEXT("class"));

	AppendChildCDATA(ClassElem, TEXT("id"), ClassId);

	const FString ClassDisplayName = FBlueprintEditorUtils::GetFriendlyClassDisplayName(Class).ToString();
	AppendChildCDATA(ClassElem, TEXT("display_name"), ClassDisplayName);

	const FString ClassTooltip = Class->GetToolTipText().ToString();
	if (ClassTooltip != ClassDisplayName)
	{
		AppendChildCDATA(ClassElem, TEXT("description"), ClassTooltip);
	}

	return true;
}

bool FNodeDocsGenerator::UpdateClassDocWithNode(FXmlFile* DocFile, UEdGraphNode* Node, const FString& NodeDesc)
{
	auto NodeId = GetNodeDocId(Node);
	auto Nodes = DocFile->GetRootNode()->FindChildNode(TEXT("nodes"));
	auto NodeElem = AppendChild(Nodes, TEXT("node"));
	AppendChildCDATA(NodeElem, TEXT("id"), NodeId);
	AppendChildCDATA(NodeElem, TEXT("shorttitle"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
	if (!NodeDesc.IsEmpty())
	{
		AppendChildCDATA(NodeElem, TEXT("description"), NodeDesc);
	}

	return true;
}

inline bool ShouldDocumentPin(UEdGraphPin* Pin)
{
	return !Pin->bHidden;
}

bool FNodeDocsGenerator::GenerateNodeDocs(UK2Node* Node, FNodeProcessingState& State)
{
	SCOPE_SECONDS_COUNTER(GenerateNodeDocsTime);

	auto NodeDocsPath = State.ClassDocsPath / TEXT("nodes");
	FString DocFilePath = NodeDocsPath / (GetNodeDocId(Node) + TEXT(".xml"));

	const FString FileTemplate = R"xxx(<?xml version="1.0" encoding="UTF-8"?>
<root></root>)xxx";

	FXmlFile File(FileTemplate, EConstructMethod::ConstructFromBuffer);
	auto Root = File.GetRootNode();

	AppendChildCDATA(Root, TEXT("docs_name"), DocsTitle);
	// Since we pull these from the class xml file, the entries are already CDATA wrapped
	AppendChildRaw(Root, TEXT("class_id"), State.ClassDocXml->GetRootNode()->FindChildNode(TEXT("id"))->GetContent());//GetClassDocId(Class));
	AppendChildRaw(Root, TEXT("class_name"), State.ClassDocXml->GetRootNode()->FindChildNode(TEXT("display_name"))->GetContent());// FBlueprintEditorUtils::GetFriendlyClassDisplayName(Class).ToString());

	FString NodeShortTitle = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
	AppendChildCDATA(Root, TEXT("shorttitle"), NodeShortTitle.TrimEnd());

	FString NodeFullTitle = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
	auto TargetIdx = NodeFullTitle.Find(TEXT("Target is "), ESearchCase::CaseSensitive);
	if (TargetIdx != INDEX_NONE)
	{
		NodeFullTitle = NodeFullTitle.Left(TargetIdx).TrimEnd();
	}
	AppendChildCDATA(Root, TEXT("fulltitle"), NodeFullTitle);

	FString NodeDesc = Node->GetTooltipText().ToString();
	TargetIdx = NodeDesc.Find(TEXT("Target is "), ESearchCase::CaseSensitive);
	if (TargetIdx != INDEX_NONE)
	{
		NodeDesc = NodeDesc.Left(TargetIdx).TrimEnd();
	}
	AppendChildCDATA(Root, TEXT("description"), NodeDesc);
	AppendChildCDATA(Root, TEXT("imgpath"), State.RelImageBasePath / State.ImageFilename);
	AppendChildCDATA(Root, TEXT("category"), Node->GetMenuCategory().ToString());

	auto Inputs = AppendChild(Root, TEXT("inputs"));
	for (auto Pin : Node->Pins)
	{
		if (Pin->Direction == EEdGraphPinDirection::EGPD_Input)
		{
			if (ShouldDocumentPin(Pin))
			{
				auto Input = AppendChild(Inputs, TEXT("param"));

				FString PinName, PinType, PinDesc;
				ExtractPinInformation(Pin, PinName, PinType, PinDesc);

				AppendChildCDATA(Input, TEXT("name"), PinName);
				AppendChildCDATA(Input, TEXT("type"), PinType);
				AppendChildCDATA(Input, TEXT("description"), PinDesc);
			}
		}
	}

	auto Outputs = AppendChild(Root, TEXT("outputs"));
	for (auto Pin : Node->Pins)
	{
		if (Pin->Direction == EEdGraphPinDirection::EGPD_Output)
		{
			if (ShouldDocumentPin(Pin))
			{
				auto Output = AppendChild(Outputs, TEXT("param"));

				FString PinName, PinType, PinDesc;
				ExtractPinInformation(Pin, PinName, PinType, PinDesc);

				AppendChildCDATA(Output, TEXT("name"), PinName);
				AppendChildCDATA(Output, TEXT("type"), PinType);
				AppendChildCDATA(Output, TEXT("description"), PinDesc);
			}
		}
	}

	if (!File.Save(DocFilePath))
	{
		return false;
	}

	if (!UpdateClassDocWithNode(State.ClassDocXml.Get(), Node, NodeDesc))
	{
		return false;
	}

	return true;
}

bool FNodeDocsGenerator::SaveIndexXml(FString const& OutDir)
{
	auto Path = OutDir / TEXT("index.xml");
	IndexXml->Save(Path);

	return true;
}

bool FNodeDocsGenerator::SaveClassDocXml(FString const& OutDir)
{
	for (auto const& Entry : ClassDocsMap)
	{
		auto ClassId = GetClassDocId(Entry.Key.Get());
		auto Path = OutDir / ClassId / (ClassId + TEXT(".xml"));
		Entry.Value->Save(Path);
	}

	return true;
}


void FNodeDocsGenerator::AdjustNodeForSnapshot(UEdGraphNode* Node)
{
	// Hide default value box containing 'self' for Target pin
	if (auto K2_Schema = Cast< UEdGraphSchema_K2 >(Node->GetSchema()))
	{
		if (auto TargetPin = Node->FindPin(K2_Schema->PN_Self))
		{
			TargetPin->bDefaultValueIsIgnored = true;
		}
	}
}

FString FNodeDocsGenerator::GetClassDocId(UClass* Class)
{
	return Class->GetName();
}

FString FNodeDocsGenerator::GetNodeDocId(UEdGraphNode* Node)
{
	// @TODO: Not sure this is right thing to use
	return Node->GetDocumentationExcerptName();
}


#include "BlueprintVariableNodeSpawner.h"
#include "BlueprintDelegateNodeSpawner.h"
#include "K2Node_CallFunction.h"
#include "K2Node_DynamicCast.h"

/*
This takes a graph node object and attempts to map it to the class which the node conceptually belong to.
If there is no special mapping for the node, the function determines the class from the source object.
*/
UClass* FNodeDocsGenerator::MapToAssociatedClass(UK2Node* NodeInst, UObject* Source)
{
	// For nodes derived from UK2Node_CallFunction, associate with the class owning the called function.
	if (auto FuncNode = Cast< UK2Node_CallFunction >(NodeInst))
	{
		auto Func = FuncNode->GetTargetFunction();
		if (Func)
		{
			return Func->GetOwnerClass();
		}
	}

	// Default fallback
	if (auto SourceClass = Cast< UClass >(Source))
	{
		return SourceClass;
	}
	else if (auto SourceBP = Cast< UBlueprint >(Source))
	{
		return SourceBP->GeneratedClass;
	}
	else
	{
		return nullptr;
	}
}

bool FNodeDocsGenerator::IsSpawnerDocumentable(UBlueprintNodeSpawner* Spawner, bool bIsBlueprint)
{
	// Spawners of or deriving from the following classes will be excluded
	static const TSubclassOf< UBlueprintNodeSpawner > ExcludedSpawnerClasses[] = {
		UBlueprintVariableNodeSpawner::StaticClass(),
		UBlueprintDelegateNodeSpawner::StaticClass(),
		UBlueprintBoundNodeSpawner::StaticClass(),
		UBlueprintComponentNodeSpawner::StaticClass(),
		UBlueprintBoundEventNodeSpawner::StaticClass()
	};

	// Spawners of or deriving from the following classes will be excluded in a blueprint context
	static const TSubclassOf< UBlueprintNodeSpawner > BlueprintOnlyExcludedSpawnerClasses[] = {
		UBlueprintEventNodeSpawner::StaticClass(),
	};

	// Spawners for nodes of these types (or their subclasses) will be excluded
	static const TSubclassOf< UK2Node > ExcludedNodeClasses[] = {
		UK2Node_DynamicCast::StaticClass(),
		UK2Node_Message::StaticClass(),
		UAnimGraphNode_Base::StaticClass()
	};

	// Function spawners for functions with any of the following metadata tags will also be excluded
	static const FName ExcludedFunctionMeta[] = {
		TEXT("BlueprintAutocast")
	};

	static const uint32 PermittedAccessSpecifiers = (FUNC_Public | FUNC_Protected);


	for (auto ExclSpawnerClass : ExcludedSpawnerClasses)
	{
		if (Spawner->IsA(ExclSpawnerClass))
		{
			return false;
		}
	}

	if (bIsBlueprint)
	{
		for (auto ExclSpawnerClass : BlueprintOnlyExcludedSpawnerClasses)
		{
			if (Spawner->IsA(ExclSpawnerClass))
			{
				return false;
			}
		}
	}

	if (!Spawner->NodeClass->IsChildOf(UK2Node::StaticClass()))
	{
		return false;
	}

	for (auto ExclNodeClass : ExcludedNodeClasses)
	{
		if (Spawner->NodeClass->IsChildOf(ExclNodeClass))
		{
			return false;
		}
	}

	if (auto FuncSpawner = Cast< UBlueprintFunctionNodeSpawner >(Spawner))
	{
		auto Func = FuncSpawner->GetFunction();

		// @NOTE: We exclude based on access level, but only if this is not a spawner for a blueprint event
		// (custom events do not have any access specifiers)
		if ((Func->FunctionFlags & FUNC_BlueprintEvent) == 0 && (Func->FunctionFlags & PermittedAccessSpecifiers) == 0)
		{
			return false;
		}

		for (auto const& Meta : ExcludedFunctionMeta)
		{
			if (Func->HasMetaData(Meta))
			{
				return false;
			}
		}
	}

	return true;
}

