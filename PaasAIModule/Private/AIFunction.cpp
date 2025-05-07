// AIFunction.cpp
#include "AIFunction.h"
#include "HttpModule.h"
#include "Json.h"
#include "JsonUtilities.h"
#include "Interfaces/IHttpResponse.h"

UDeepSeekFunction* UDeepSeekFunction::SendRequest(const FDeepSeekRequestParams& Params)
{
    UDeepSeekFunction* Function = NewObject<UDeepSeekFunction>();
    Function->bDebug = Params.bDebugMode;
    Function->ExecuteRequest(Params);
    return Function;
}

UDeepSeekFunction* UDeepSeekFunction::QuickSendRequest(const FString& APIKey, const FString& Prompt, bool UseStreaming, bool Debug)
{
    FDeepSeekRequestParams Params;
    Params.APIKey = APIKey;
    Params.bStream = UseStreaming;
    Params.bDebugMode = Debug;
    
    // 预分配容量以提高性能
    Params.Messages.Reserve(1);
    Params.Messages.Add(FDeepSeekMessage(TEXT("user"), Prompt));
    
    return SendRequest(Params);
}

UDeepSeekFunction* UDeepSeekFunction::SendConversationMessage(
    const TArray<FDeepSeekMessage>& Messages, 
    const FString& NewUserMessage,
    const FString& APIKey, 
    const FString& Model,
    bool UseStreaming, 
    bool Debug)
{
    FDeepSeekRequestParams Params;
    Params.APIKey = APIKey;
    Params.Model = Model;
    Params.bStream = UseStreaming;
    Params.bDebugMode = Debug;
    
    // 预分配足够的容量以避免重新分配
    Params.Messages.Reserve(Messages.Num() + 1);
    Params.Messages.Append(Messages);
    Params.Messages.Add(FDeepSeekMessage(TEXT("user"), NewUserMessage));
    
    return SendRequest(Params);
}

void UDeepSeekFunction::LogDebug(const FString& Message, bool bIsError)
{
    if (!bDebug) return;
    
    if (bIsError)
    {
        UE_LOG(LogTemp, Error, TEXT("[DeepSeek] %s"), *Message);
    }
    else
    {
        UE_LOG(LogTemp, Display, TEXT("[DeepSeek] %s"), *Message);
    }
    OnDebugMessage.Broadcast(Message);
}

void UDeepSeekFunction::BeginDestroy()
{
    bIsBeingDestroyed = true;
    
    // 确保没有被请求持有
    if (HttpRequestRef.IsValid())
    {
        HttpRequestRef->CancelRequest();
        HttpRequestRef.Reset();
    }
    
    SafeRemoveFromRoot();
    Super::BeginDestroy();
}

void UDeepSeekFunction::SafeRemoveFromRoot()
{
    // 安全地从根集中移除
    if (IsRooted())
    {
        RemoveFromRoot();
    }
}

void UDeepSeekFunction::ExecuteRequest(const FDeepSeekRequestParams& Params)
{
    // 重置状态
    AccumulatedStreamText.Empty();
    bIsRequestComplete = false;
    bIsBeingDestroyed = false;
    
    // 加入根集，防止被垃圾回收
    AddToRoot();

    LogDebug(FString::Printf(TEXT("Starting request to: %s"), *Params.URL));

    // 创建并保存HTTP请求引用
    HttpRequestRef = FHttpModule::Get().CreateRequest();
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = HttpRequestRef.ToSharedRef();
    
    // 设置URL和头部
    HttpRequest->SetURL(Params.URL);
    HttpRequest->SetVerb(TEXT("POST"));
    HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *Params.APIKey));

    // 构建JSON请求体
    TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
    JsonObject->SetStringField(TEXT("model"), Params.Model);
    
    // 添加消息数组
    TArray<TSharedPtr<FJsonValue>> MessagesArray;
    MessagesArray.Reserve(Params.Messages.Num());
    for (const FDeepSeekMessage& Message : Params.Messages)
    {
        TSharedPtr<FJsonObject> MessageObject = MakeShareable(new FJsonObject);
        MessageObject->SetStringField(TEXT("role"), Message.Role);
        MessageObject->SetStringField(TEXT("content"), Message.Content);
        MessagesArray.Add(MakeShareable(new FJsonValueObject(MessageObject)));
    }
    
    JsonObject->SetArrayField(TEXT("messages"), MessagesArray);
    JsonObject->SetBoolField(TEXT("stream"), Params.bStream);
    JsonObject->SetNumberField(TEXT("temperature"), FMath::Clamp(Params.Temperature, 0.0f, 1.0f));
    JsonObject->SetNumberField(TEXT("max_tokens"), FMath::Max(1, Params.MaxTokens));
    
    // 序列化JSON
    FString RequestBody;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
    FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

    HttpRequest->SetContentAsString(RequestBody);
    
    LogDebug(FString::Printf(TEXT("Request Body: %s"), *RequestBody));

    // 处理流式响应
    if (Params.bStream)
    {
        HttpRequest->SetResponseBodyReceiveStreamDelegate(
            FHttpRequestStreamDelegate::CreateWeakLambda(this, [this](void* Data, int64 Length) -> bool {
                if (bIsBeingDestroyed || Length <= 0) return false;
                
                FString DataString = FString(UTF8_TO_TCHAR(static_cast<const char*>(Data)));
                return HandleStreamData(DataString);
            })
        );
    }

    // 绑定请求完成回调
    HttpRequest->OnProcessRequestComplete().BindWeakLambda(this, 
        [this](FHttpRequestPtr RequestPtr, FHttpResponsePtr Response, bool bWasSuccessful)
    {
        // 如果对象正在被销毁，不执行任何回调
        if (bIsBeingDestroyed)
        {
            SafeRemoveFromRoot();
            return;
        }
        
        // 请求完成，处理失败情况
        if (!bWasSuccessful || !Response.IsValid())
        {
            FString ErrorMessage = TEXT("Request failed");
            if (Response.IsValid())
            {
                ErrorMessage = FString::Printf(TEXT("Request failed with code %d: %s"), 
                                           Response->GetResponseCode(), 
                                           *Response->GetContentAsString());
            }
            OnFailed.Broadcast(ErrorMessage);
            LogDebug(ErrorMessage, true);
            SafeRemoveFromRoot();
            return;
        }
        
        // 获取响应字符串
        FString ResponseContent = Response->GetContentAsString();
        LogDebug(FString::Printf(TEXT("Received response (length: %d bytes)"), ResponseContent.Len()));
        
        // 流式响应处理
        if (!AccumulatedStreamText.IsEmpty())
        {
            // 流式响应已在HandleStreamData中处理过
            if (!bIsRequestComplete)
            {
                bIsRequestComplete = true;
                OnCompleted.Broadcast(AccumulatedStreamText);
            }
        }
        else
        {
            // 非流式响应，解析JSON
            TSharedPtr<FJsonObject> JsonResponse;
            TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseContent);
            
            if (FJsonSerializer::Deserialize(Reader, JsonResponse) && JsonResponse.IsValid())
            {
                // 检查错误
                if (JsonResponse->HasField(TEXT("error")))
                {
                    const TSharedPtr<FJsonObject>* ErrorObj;
                    if (JsonResponse->TryGetObjectField(TEXT("error"), ErrorObj))
                    {
                        FString ErrorMessage;
                        if ((*ErrorObj)->TryGetStringField(TEXT("message"), ErrorMessage))
                        {
                            OnFailed.Broadcast(ErrorMessage);
                            LogDebug(FString::Printf(TEXT("Error: %s"), *ErrorMessage), true);
                            SafeRemoveFromRoot();
                            return;
                        }
                    }
                    OnFailed.Broadcast(TEXT("Unknown API error"));
                    SafeRemoveFromRoot();
                    return;
                }
                
                // 提取内容
                FString Content = ExtractContentFromResponse(ResponseContent);
                bIsRequestComplete = true;
                OnCompleted.Broadcast(Content);
            }
            else
            {
                // 无法解析JSON
                bIsRequestComplete = true;
                OnCompleted.Broadcast(ResponseContent);
            }
        }
        
        // 清理请求引用并从根集移除
        HttpRequestRef.Reset();
        SafeRemoveFromRoot();
    });

    LogDebug(TEXT("Sending request..."));
    HttpRequest->ProcessRequest();
}

bool UDeepSeekFunction::HandleStreamData(const FString& DataString)
{
    // 防止在对象销毁过程中处理数据
    if (bIsBeingDestroyed)
    {
        return false;
    }
    
    // 处理SSE格式数据 (data: {...})
    TArray<FString> Lines;
    DataString.ParseIntoArrayLines(Lines);
    
    for (const FString& Line : Lines)
    {
        if (Line.IsEmpty())
            continue;
            
        FString TrimmedLine = Line.TrimStartAndEnd();
        
        // 检查是否是SSE数据行
        if (TrimmedLine.StartsWith(TEXT("data:")))
        {
            FString JsonData = TrimmedLine.RightChop(5).TrimStartAndEnd();
            
            // 检查结束标记
            if (JsonData == TEXT("[DONE]"))
            {
                // 流式响应结束
                if (!bIsRequestComplete)
                {
                    bIsRequestComplete = true;
                    OnCompleted.Broadcast(AccumulatedStreamText);
                }
                continue;
            }
            
            // 解析JSON
            TSharedPtr<FJsonObject> JsonObject;
            TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonData);
            
            if (FJsonSerializer::Deserialize(Reader, JsonObject))
            {
                // 获取choices
                const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
                if (JsonObject->TryGetArrayField(TEXT("choices"), Choices) && Choices->Num() > 0)
                {
                    // 获取delta
                    TSharedPtr<FJsonObject> ChoiceObject = (*Choices)[0]->AsObject();
                    const TSharedPtr<FJsonObject>* DeltaObject = nullptr;
                    
                    if (ChoiceObject->TryGetObjectField(TEXT("delta"), DeltaObject))
                    {
                        // 获取content
                        FString Content;
                        if ((*DeltaObject)->TryGetStringField(TEXT("content"), Content) && !Content.IsEmpty())
                        {
                            // 累积文本
                            AccumulatedStreamText.Append(Content);
                            
                            // 触发事件
                            OnStream.Broadcast(Content);
                            LogDebug(FString::Printf(TEXT("Stream content length: %d"), Content.Len()));
                        }
                    }
                }
            }
        }
    }
    return true;
}

FString UDeepSeekFunction::ExtractContentFromResponse(const FString& ResponseString)
{
    TSharedPtr<FJsonObject> JsonResponse;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseString);
    
    if (FJsonSerializer::Deserialize(Reader, JsonResponse))
    {
        // 获取choices
        const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
        if (JsonResponse->TryGetArrayField(TEXT("choices"), Choices) && Choices->Num() > 0)
        {
            // 获取message
            TSharedPtr<FJsonObject> ChoiceObject = (*Choices)[0]->AsObject();
            const TSharedPtr<FJsonObject>* MessageObject = nullptr;
            
            if (ChoiceObject->TryGetObjectField(TEXT("message"), MessageObject))
            {
                FString Content;
                if ((*MessageObject)->TryGetStringField(TEXT("content"), Content))
                {
                    return Content;
                }
            }
        }
    }
    
    // 无法解析
    return ResponseString;
}
