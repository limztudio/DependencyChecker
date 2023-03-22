#include "DependencyCheckCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Internationalization/Regex.h"

#include "TaskGroup.hpp"
#include "Containers/Queue.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_DependencyCheck
{
	typedef TMap<FName, TSet<FName>> InputTableType;


	static const FName PathGame(TEXT("/Game"));
	static const FString PathGameAsString(PathGame.ToString());

	static const FString ExternalPatternString(TEXT("/__External[^/]*__/"));

	
	static TArray<TTuple<FName, TArray<FName>>> CheckIfAnyReferencesSpecificDirectory(const InputTableType& InputTable, const FName& SearchingDirectory)
	{
		const FString SearchingDirectoryAsString = SearchingDirectory.ToString();

		FString WholeSearchingDirectoryAsString = PathGameAsString + SearchingDirectoryAsString;
		const FName WholeSearchingDirectory = FName(WholeSearchingDirectoryAsString);
		WholeSearchingDirectoryAsString = WholeSearchingDirectory.ToString();
		
		static const FRegexPattern ExternalPattern0(ExternalPatternString);
		const FRegexPattern ExternalPattern1(ExternalPatternString + SearchingDirectoryAsString);

		TArray<TTuple<FName, TArray<FName>>> Result;
		
		FString DependentPackageNameAsString;
		for (const auto& WrappedInput : InputTable)
		{
			const FName& CurPackageName = WrappedInput.Get<0>();
			FString CurPackageNameAsString = CurPackageName.ToString();

			if (FRegexMatcher(ExternalPattern1, CurPackageNameAsString).FindNext())
			{
				continue;
			}
			if (CurPackageNameAsString.StartsWith(WholeSearchingDirectoryAsString))
			{
				continue;
			}

			TArray<FName> CurrentList;
			for (const FName& DependentPackageName : WrappedInput.Get<1>())
			{
				DependentPackageNameAsString = DependentPackageName.ToString();

				if (!DependentPackageNameAsString.StartsWith(PathGameAsString) || FRegexMatcher(ExternalPattern0, DependentPackageNameAsString).FindNext())
				{
					continue;
				}
				if (!DependentPackageNameAsString.StartsWith(WholeSearchingDirectoryAsString))
				{
					continue;
				}

				CurrentList.Emplace(DependentPackageName);
			}

			if (CurrentList.Num() > 0)
			{
				Result.Emplace(TTuple<FName, TArray<FName>>(CurPackageName, MoveTemp(CurrentList)));
			}
		}

		return Result;
	}

	static void CheckIfAnyHasCircularDependencyRecursively(const InputTableType* InputTable, const FName& LeftPackageName, const FName& RightPackageName, FTaskGroup* TaskGroup, TQueue<FName, EQueueMode::Mpsc>* Elements)
	{
		if (LeftPackageName == RightPackageName)
		{
			Elements->Enqueue(LeftPackageName);
			return;
		}

		const auto* NextList = InputTable->Find(RightPackageName);
		if (!NextList)
		{
			return;
		}
		for (const FName& NextPackageName : (*NextList))
		{
			TaskGroup->Run([InputTable, LeftPackageName, NextPackageName, TaskGroup, Elements](){ CheckIfAnyHasCircularDependencyRecursively(InputTable, LeftPackageName,NextPackageName, TaskGroup, Elements); });
		}
	}
	static TArray<FName> CheckIfAnyHasCircularDependency(const InputTableType& InputTable)
	{
		FTaskGroup TaskGroup;
		TQueue<FName, EQueueMode::Mpsc> Elements;
		
		for (const auto& WrappedLeft : InputTable)
		{
			const FName& LeftPackageName = WrappedLeft.Get<0>();

			for (const FName& RightPackageName : WrappedLeft.Get<1>())
			{
				CheckIfAnyHasCircularDependencyRecursively(&InputTable, LeftPackageName, RightPackageName, &TaskGroup, &Elements);
			}
		}
		TaskGroup.Wait();
		
		TArray<FName> Result;

		FName TmpName;
		TSet<FName> TmpTable;
		while (Elements.Dequeue(TmpName))
		{
			bool bHasValue = false;
			TmpTable.Emplace(TmpName, &bHasValue);
			if (bHasValue)
			{
				continue;
			}

			Result.Emplace(MoveTemp(TmpName));
		}

		return Result;
	}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int32 UDependencyCheckCommandlet::Main(const FString& Params)
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	__hidden_DependencyCheck::InputTableType AssetTable;
	{
		TArray<FAssetData> AssetDataList;
		{
			FARFilter Filter;
			Filter.bRecursivePaths = true;
			Filter.PackagePaths.Emplace(__hidden_DependencyCheck::PathGame);
			AssetRegistry.GetAssets(Filter, AssetDataList);
		}
		if (AssetDataList.Num() <= 0)
		{
			return 0;
		}

		for (const FAssetData& AssetData : AssetDataList)
		{
			AssetTable.Emplace(AssetData.PackageName, TSet<FName>());
		}
		if (AssetTable.Num() <= 0)
		{
			return 0;
		}

		TArray<FName> DependentArray;
		for (auto It = AssetTable.CreateIterator(); It; ++It)
		{
			DependentArray.Reset();
			AssetRegistry.GetDependencies(It->Get<0>(), DependentArray, UE::AssetRegistry::EDependencyCategory::Package, UE::AssetRegistry::FDependencyQuery(UE::AssetRegistry::EDependencyQuery::Hard));
			if (DependentArray.Num() <= 0)
			{
				It.RemoveCurrent();
				continue;
			}

			TSet<FName>& DependentSet = It->Get<1>();
			for (FName& Name : DependentArray)
			{
				DependentSet.Emplace(MoveTemp(Name));
			}
			if (DependentSet.Num() <= 0)
			{
				It.RemoveCurrent();
				continue;
			}
		}
	}
	
	__hidden_DependencyCheck::CheckIfAnyReferencesSpecificDirectory(AssetTable, TEXT("Developers"));

	__hidden_DependencyCheck::CheckIfAnyHasCircularDependency(AssetTable);

	return 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

