// SimpleChat.h
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "AIFunction.h"
#include "SimpleChat.generated.h"

/**
 * 简化的DeepSeek聊天接口，自动管理会话
 */
UCLASS(Blueprintable, BlueprintType)
class PAASAIMODULE_API USimpleChat : public UObject
{
	GENERATED_BODY()

public:
	/** 流式响应 */
	UPROPERTY(BlueprintAssignable)
	FDeepSeekResponse OnStream;
    
	/** 请求完成 */
	UPROPERTY(BlueprintAssignable)
	FDeepSeekResponse OnCompleted;
    
	/** 请求失败 */
	UPROPERTY(BlueprintAssignable)
	FDeepSeekResponse OnFailed;
    
	/**
	 * 创建新的聊天实例
	 */
	UFUNCTION(BlueprintCallable, Category = "AI|DeepSeek")
	static USimpleChat* CreateChatInstance();
    
	/** 
	 * 发送消息并获取响应，自动维护对话上下文
	 */
	UFUNCTION(BlueprintCallable, Category = "AI|DeepSeek")
	void SendMessage(
		const FString& APIKey,
		const FString& Message,
		const FString& SystemPrompt = TEXT(""),
		const FString& ModelName = TEXT("deepseek-chat"),
		float Temperature = 0.7f);
    
	/** 
	 * 获取当前对话历史
	 */
	UFUNCTION(BlueprintPure, Category = "AI|DeepSeek")
	FString GetChatHistory() const;
    
	/**
	 * 清除对话历史，开始新对话
	 */
	UFUNCTION(BlueprintCallable, Category = "AI|DeepSeek")
	void ClearChat();

	/**
	 * 获取所有消息
	 */
	UFUNCTION(BlueprintCallable, Category = "AI|DeepSeek")
	const TArray<FDeepSeekMessage>& GetMessages() const;
	
	/**
	 * 对象销毁时的清理
	 */
	virtual void BeginDestroy() override;

private:    
	UFUNCTION()
	void HandleStreamResponse(FString Response);
    
	UFUNCTION()
	void HandleCompletedResponse(FString Response);
    
	UFUNCTION()
	void HandleFailedResponse(FString ErrorMessage);
	
	// 清理当前请求
	void CleanupCurrentRequest();

	// 会话历史
	UPROPERTY()
	TArray<FDeepSeekMessage> ChatHistory;
    
	UPROPERTY()
	UDeepSeekFunction* ApiRequest = nullptr;
    
	FString AccumulatedResponse;
	bool bIsBeingDestroyed = false;
};
