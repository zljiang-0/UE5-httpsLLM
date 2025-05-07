// SimpleChat.cpp
#include "SimpleChat.h"

USimpleChat* USimpleChat::CreateChatInstance()
{
    return NewObject<USimpleChat>();
}

const TArray<FDeepSeekMessage>& USimpleChat::GetMessages() const
{
    return ChatHistory;
}

void USimpleChat::BeginDestroy()
{
    bIsBeingDestroyed = true;
    CleanupCurrentRequest();
    Super::BeginDestroy();
}

void USimpleChat::CleanupCurrentRequest()
{
    // 清除当前的请求及其回调
    if (ApiRequest != nullptr)
    {
        ApiRequest->OnStream.RemoveAll(this);
        ApiRequest->OnCompleted.RemoveAll(this);
        ApiRequest->OnFailed.RemoveAll(this);
        ApiRequest = nullptr;
    }
}

void USimpleChat::SendMessage(
    const FString& APIKey,
    const FString& Message,
    const FString& SystemPrompt,
    const FString& ModelName,
    float Temperature)
{
    // 检查是否在销毁过程中
    if (bIsBeingDestroyed)
    {
        return;
    }

    // 验证输入参数
    if (Message.IsEmpty())
    {
        OnFailed.Broadcast(TEXT("Cannot send empty message"));
        return;
    }
    
    if (APIKey.IsEmpty())
    {
        OnFailed.Broadcast(TEXT("API Key is required"));
        return;
    }
    
    // 重置累积响应
    AccumulatedResponse.Empty();
    
    // 确保清理之前的请求
    CleanupCurrentRequest();
    
    // 如果是新对话且有系统提示，添加系统消息
    if (ChatHistory.Num() == 0 && !SystemPrompt.IsEmpty())
    {
        ChatHistory.Add(FDeepSeekMessage(TEXT("system"), SystemPrompt));
    }
    
    // 添加用户消息到历史
    ChatHistory.Add(FDeepSeekMessage(TEXT("user"), Message));
    
    // 创建请求参数
    FDeepSeekRequestParams Params;
    Params.APIKey = APIKey;
    Params.Model = ModelName;
    Params.Messages = ChatHistory;
    Params.bStream = true;
    Params.Temperature = FMath::Clamp(Temperature, 0.0f, 1.0f);
    
    // 发送请求
    ApiRequest = UDeepSeekFunction::SendRequest(Params);
    
    // 绑定回调
    if (ApiRequest)
    {
        ApiRequest->OnStream.AddDynamic(this, &USimpleChat::HandleStreamResponse);
        ApiRequest->OnCompleted.AddDynamic(this, &USimpleChat::HandleCompletedResponse);
        ApiRequest->OnFailed.AddDynamic(this, &USimpleChat::HandleFailedResponse);
    }
    else
    {
        OnFailed.Broadcast(TEXT("Failed to create API request"));
    }
}

FString USimpleChat::GetChatHistory() const
{
    FString History;
    History.Reserve(1024); // 预分配一个合理的初始容量
    
    for (const FDeepSeekMessage& Message : ChatHistory)
    {
        FString Prefix;
        if (Message.Role == TEXT("user"))
        {
            Prefix = TEXT("User: ");
        }
        else if (Message.Role == TEXT("assistant"))
        {
            Prefix = TEXT("Assistant: ");
        }
        else if (Message.Role == TEXT("system"))
        {
            Prefix = TEXT("System: ");
        }
        
        History.Append(Prefix);
        History.Append(Message.Content);
        History.Append(TEXT("\n\n"));
    }
    
    return History;
}

void USimpleChat::ClearChat()
{
    ChatHistory.Empty();
    CleanupCurrentRequest();
}

void USimpleChat::HandleStreamResponse(FString Response)
{
    if (bIsBeingDestroyed || Response.IsEmpty()) return;
    
    // 累积流式响应
    AccumulatedResponse.Append(Response);
    
    // 转发流式响应
    OnStream.Broadcast(Response);
}

void USimpleChat::HandleCompletedResponse(FString Response)
{
    if (bIsBeingDestroyed) return;
    
    // 获取完整的流式响应
    FString FullResponse;
    if (ApiRequest)
    {
        FullResponse = ApiRequest->GetFullStreamedText();
    }
    
    if (FullResponse.IsEmpty())
    {
        FullResponse = AccumulatedResponse;
    }
    
    if (!FullResponse.IsEmpty())
    {
        // 添加助手响应到历史
        ChatHistory.Add(FDeepSeekMessage(TEXT("assistant"), FullResponse));
        
        // 触发完成事件
        OnCompleted.Broadcast(FullResponse);
    }
    else
    {
        // 响应为空
        OnFailed.Broadcast(TEXT("Received empty response"));
    }
    
    // 清理请求
    CleanupCurrentRequest();
}

void USimpleChat::HandleFailedResponse(FString ErrorMessage)
{
    if (bIsBeingDestroyed) return;
    
    // 触发失败事件
    OnFailed.Broadcast(ErrorMessage);
    
    // 清理请求
    CleanupCurrentRequest();
}
