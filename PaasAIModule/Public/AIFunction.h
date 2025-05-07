// AIFunction.h
#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "AIFunction.generated.h"

/**
 * 单条消息结构体
 */
USTRUCT(BlueprintType)
struct FDeepSeekMessage
{
    GENERATED_BODY()
    
    /** 消息角色 (system, user, assistant) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DeepSeek")
    FString Role = TEXT("user");
    
    /** 消息内容 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DeepSeek", meta = (MultiLine = true))
    FString Content;
    
    FDeepSeekMessage() {}
    FDeepSeekMessage(const FString& InRole, const FString& InContent)
        : Role(InRole), Content(InContent) {}
};

/**
 * 请求参数结构体
 */
USTRUCT(BlueprintType)
struct FDeepSeekRequestParams
{
    GENERATED_BODY()
    
    /** API URL */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DeepSeek")
    FString URL = TEXT("https://api.deepseek.com/v1/chat/completions");
    
    /** API密钥 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DeepSeek")
    FString APIKey;
    
    /** 模型名称 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DeepSeek")
    FString Model = TEXT("deepseek-chat");
    
    /** 消息数组 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DeepSeek")
    TArray<FDeepSeekMessage> Messages;
    
    /** 是否使用流式响应 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DeepSeek")
    bool bStream = true;
    
    /** 温度参数 (0.0-1.0) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DeepSeek", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float Temperature = 0.7f;
    
    /** 最大令牌数 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DeepSeek")
    int32 MaxTokens = 2048;
    
    /** 调试模式 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DeepSeek")
    bool bDebugMode = false;
};

/** 响应委托 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDeepSeekResponse, FString, Response);
/** 调试信息委托 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDeepSeekDebug, FString, Message);

/**
 * DeepSeek API异步调用节点
 */
UCLASS()
class PAASAIMODULE_API UDeepSeekFunction : public UBlueprintAsyncActionBase
{
    GENERATED_BODY()

public:
    /** 请求完成时触发 */
    UPROPERTY(BlueprintAssignable)
    FDeepSeekResponse OnCompleted;
    
    /** 请求失败时触发 */
    UPROPERTY(BlueprintAssignable)
    FDeepSeekResponse OnFailed;
    
    /** 流式响应时触发 */
    UPROPERTY(BlueprintAssignable)
    FDeepSeekResponse OnStream;
    
    /** 调试信息 */
    UPROPERTY(BlueprintAssignable)
    FDeepSeekDebug OnDebugMessage;

    /**
     * 发送DeepSeek请求(高级版)
     */
    UFUNCTION(BlueprintCallable, Category = "AI|DeepSeek", meta = (BlueprintInternalUseOnly = "true"))
    static UDeepSeekFunction* SendRequest(const FDeepSeekRequestParams& Params);
    
    /**
     * 快速发送请求(简化版)
     */
    UFUNCTION(BlueprintCallable, Category = "AI|DeepSeek", meta = (BlueprintInternalUseOnly = "true"))
    static UDeepSeekFunction* QuickSendRequest(const FString& APIKey, const FString& Prompt, bool UseStreaming = true, bool Debug = false);

    /**
     * 发送多轮对话消息
     */
    UFUNCTION(BlueprintCallable, Category = "AI|DeepSeek", meta = (BlueprintInternalUseOnly = "true"))
    static UDeepSeekFunction* SendConversationMessage(
        const TArray<FDeepSeekMessage>& Messages, 
        const FString& NewUserMessage,
        const FString& APIKey, 
        const FString& Model = TEXT("deepseek-chat"),
        bool UseStreaming = true, 
        bool Debug = true);

    // 获取流式传输的完整文本
    UFUNCTION(BlueprintPure, Category = "AI|DeepSeek")
    const FString& GetFullStreamedText() const { return AccumulatedStreamText; }
    
    // 析构函数，确保对象从根集中移除
    virtual void BeginDestroy() override;

protected:
    void ExecuteRequest(const FDeepSeekRequestParams& Params);
    void LogDebug(const FString& Message, bool bIsError = false);
    bool HandleStreamData(const FString& DataString);
    FString ExtractContentFromResponse(const FString& ResponseString);
    
    // 从根集中移除自身的安全方法
    void SafeRemoveFromRoot();
    
    bool bDebug = false;
    FString AccumulatedStreamText;
    bool bIsRequestComplete = false;
    bool bIsBeingDestroyed = false;
    
    // 指向HTTP请求的强引用，防止被垃圾回收
    TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> HttpRequestRef;
};
