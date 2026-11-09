import logging
from contextlib import asynccontextmanager
from typing import Any
import os
import time

import uvicorn
from fastapi import FastAPI, File, Form, HTTPException, Request, UploadFile, status
from fastapi.responses import JSONResponse
from pydantic import BaseModel

try:
    from faster_whisper import WhisperModel
except ImportError as exc:
    raise SystemExit(
        "faster-whisper 未安裝：請先執行 .venv/Scripts/python.exe -m pip install -r requirements.txt"
    ) from exc

from app_logic import transcribe_wav_bytes
from ollama_client import AIClient

logger = logging.getLogger("whisper-stt")
SERVER_VERSION = "1.2.0"


class AppState:
    def __init__(self):
        self.whisper = None
        self.ai = None


state = AppState()


class ChatMessage(BaseModel):
    role: str = "user"
    content: str = ""


class ChatCompletionRequest(BaseModel):
    model: str | None = None
    messages: list[ChatMessage] | None = None
    temperature: float | None = None
    max_tokens: int | None = None
    stream: bool = False


def _error(message: str, code: int = status.HTTP_500_INTERNAL_SERVER_ERROR) -> JSONResponse:
    """OpenAI / Ollama 風格的錯誤回應，讓 ESP32 能解析 error.message。"""
    return JSONResponse(
        status_code=code,
        content={"error": {"message": message, "type": "server_error"}},
    )


def _reply_of(messages: list[dict[str, str]], temperature: float | None = None,
              max_tokens: int | None = None) -> str:
    ai = state.ai
    if ai is None:
        raise RuntimeError("AI 客戶端尚未初始化")

    cleaned = [
        {"role": str(m.get("role") or "user"), "content": str(m.get("content") or "")}
        for m in messages
        if m.get("content")
    ]
    if not cleaned:
        raise ValueError("messages 不能為空")

    reply = ai.chat(cleaned, temperature=temperature, max_tokens=max_tokens)
    reply = (reply or "").strip()
    if not reply:
        raise RuntimeError("AI 未回傳任何內容")
    return reply


@asynccontextmanager
async def lifespan(app: FastAPI):
    model_name = os.environ.get("WHISPER_MODEL", "base")
    model_dir = os.environ.get("WHISPER_MODEL_DIR") or None
    device = "cuda" if _cuda_available() else "cpu"
    compute_type = _compute_type_for(device)

    logger.info(
        "whisper-stt-server starting model=%s device=%s compute_type=%s model_dir=%s",
        model_name, device, compute_type, model_dir,
    )

    state.whisper = WhisperModel(
        model_name, device=device, compute_type=compute_type, download_root=model_dir
    )
    logger.info("Whisper model loaded: %s (%s)", model_name, device)

    ai_mode = os.environ.get("AI_MODE", "ollama")
    ai_base_url = os.environ.get("AI_BASE_URL", "http://localhost:11434")
    ai_api_key = os.environ.get("AI_API_KEY", "")
    ai_model = os.environ.get("AI_MODEL", "qwen3.5:9b")
    ai_system_prompt = os.environ.get(
        "AI_SYSTEM_PROMPT",
        "你是住在這台 ESP32 小相機裡的 AI 助理，個性友善、有點幽默。"
        "一律使用繁體中文（台灣用語）回答，最多 80 個字，語氣口語化。"
        "不要使用 Markdown、標題、條列符號或表情符號。"
    )
    ai_temperature = float(os.environ.get("AI_TEMPERATURE", "0.7"))
    ai_max_tokens = int(os.environ.get("AI_MAX_TOKENS", "512"))

    state.ai = AIClient(
        mode=ai_mode,
        base_url=ai_base_url,
        api_key=ai_api_key,
        model=ai_model,
        system_prompt=ai_system_prompt,
        temperature=ai_temperature,
        max_tokens=ai_max_tokens,
    )
    logger.info(
        "AI client initialized: mode=%s base_url=%s model=%s",
        ai_mode, ai_base_url, ai_model,
    )

    yield

    if state.whisper is not None:
        del state.whisper
        state.whisper = None
    if state.ai is not None:
        state.ai.close()
        state.ai = None


def _cuda_available() -> bool:
    try:
        import torch
        return torch.cuda.is_available()
    except Exception:
        return False


def _compute_type_for(device: str) -> str:
    if device == "cuda":
        return os.environ.get("WHISPER_COMPUTE_TYPE", "int8_fp16")
    return "int8"


app = FastAPI(title="whisper-stt-server", version=SERVER_VERSION, lifespan=lifespan)


@app.get("/health")
async def health() -> dict[str, Any]:
    return {
        "ok": True,
        "service": "whisper-stt-server",
        "version": SERVER_VERSION,
        "whisper_loaded": state.whisper is not None,
        "ai_ready": state.ai is not None,
    }


# ---------------------------------------------------------------------------
#  模型列表（OpenAI 相容 / Ollama 相容）
# ---------------------------------------------------------------------------
@app.get("/v1/models")
@app.get("/models")
async def list_models() -> dict[str, Any]:
    data = [
        {
            "id": os.environ.get("WHISPER_MODEL", "base"),
            "object": "model",
            "created": 0,
            "owned_by": "whisper-stt-server",
        }
    ]
    ai = state.ai
    if ai is not None:
        data.append(
            {
                "id": ai.model,
                "object": "model",
                "created": 0,
                "owned_by": "ollama" if ai.mode == "ollama" else "openai-compatible",
            }
        )
    return {"object": "list", "data": data}


@app.get("/api/tags")
async def ollama_tags() -> dict[str, Any]:
    return {
        "models": [
            {"name": item["id"], "model": item["id"]}
            for item in (await list_models())["data"]
        ]
    }


# ---------------------------------------------------------------------------
#  語音轉文字（OpenAI 相容，含 /v1 與無前綴兩種路徑）
# ---------------------------------------------------------------------------
@app.post("/audio/transcriptions")
@app.post("/v1/audio/transcriptions")
async def audio_transcriptions(
    file: UploadFile = File(...),
    model: str | None = Form(None),
    language: str | None = Form(None),
) -> JSONResponse:
    start = time.perf_counter()
    raw = await file.read()
    if not raw:
        raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="空的音檔")

    whisper = state.whisper
    if whisper is None:
        raise HTTPException(status_code=status.HTTP_503_SERVICE_UNAVAILABLE, detail="模型尚未載入")

    try:
        text = await transcribe_wav_bytes(
            whisper=whisper,
            audio_bytes=raw,
            model_hint=model,
            language_hint=language,
        )
    except ValueError as exc:
        raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail=str(exc)) from exc
    except Exception as exc:
        logger.exception("transcription failed")
        raise HTTPException(status_code=status.HTTP_500_INTERNAL_SERVER_ERROR, detail="語音辨識失敗") from exc

    elapsed_ms = int((time.perf_counter() - start) * 1000)
    logger.info(
        "transcribed model=%s language=%s size=%d len=%d elapsed_ms=%d",
        model or "default", language or "auto", len(raw), len(text), elapsed_ms,
    )

    return JSONResponse(content={"text": text})


@app.post("/transcribe")
@app.post("/v1/transcribe")
async def transcribe_raw(
    file: UploadFile = File(...),
    model: str | None = Form(None),
    language: str | None = Form(None),
) -> JSONResponse:
    return await audio_transcriptions(file=file, model=model, language=language)


# ---------------------------------------------------------------------------
#  對話（Ollama 相容 /api/chat）
# ---------------------------------------------------------------------------
@app.post("/api/chat")
async def ollama_chat(request: Request) -> JSONResponse:
    try:
        body = await request.json()
    except Exception:
        return _error("無效的 JSON", status.HTTP_400_BAD_REQUEST)

    if not isinstance(body, dict):
        return _error("無效的請求內容", status.HTTP_400_BAD_REQUEST)

    messages = body.get("messages") or []
    if not isinstance(messages, list) or not messages:
        return _error("messages 不能為空", status.HTTP_400_BAD_REQUEST)

    options = body.get("options") or {}
    temperature = options.get("temperature")
    max_tokens = options.get("num_predict")
    try:
        temperature = None if temperature is None else float(temperature)
        max_tokens = None if max_tokens is None else int(max_tokens)
    except (TypeError, ValueError):
        return _error("options 格式錯誤", status.HTTP_400_BAD_REQUEST)

    try:
        reply = _reply_of(messages, temperature=temperature, max_tokens=max_tokens)
    except ValueError as exc:
        return _error(str(exc), status.HTTP_400_BAD_REQUEST)
    except Exception:
        logger.exception("AI chat failed")
        return _error("AI 語意理解失敗")

    ai = state.ai
    return JSONResponse(
        content={
            "model": body.get("model") or (ai.model if ai else "unknown"),
            "created_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "message": {"role": "assistant", "content": reply},
            "done": True,
            "done_reason": "stop",
        }
    )


# ---------------------------------------------------------------------------
#  對話（OpenAI 相容 /chat/completions）
# ---------------------------------------------------------------------------
@app.post("/chat/completions")
@app.post("/v1/chat/completions")
async def chat_completions(req: ChatCompletionRequest) -> JSONResponse:
    if not req.messages:
        return _error("messages 不能為空", status.HTTP_400_BAD_REQUEST)

    ai = state.ai
    if ai is None:
        return _error("AI 客戶端尚未初始化", status.HTTP_503_SERVICE_UNAVAILABLE)

    messages = [{"role": m.role or "user", "content": m.content} for m in req.messages]
    try:
        reply = _reply_of(messages, temperature=req.temperature, max_tokens=req.max_tokens)
    except ValueError as exc:
        return _error(str(exc), status.HTTP_400_BAD_REQUEST)
    except Exception:
        logger.exception("AI chat failed")
        return _error("AI 語意理解失敗")

    approx_tokens = max(1, len(reply) // 4)
    return JSONResponse(
        content={
            "id": f"chatcmpl-{time.time_ns()}",
            "object": "chat.completion",
            "created": int(time.time()),
            "model": req.model or ai.model,
            "choices": [
                {
                    "index": 0,
                    "message": {"role": "assistant", "content": reply},
                    "finish_reason": "stop",
                }
            ],
            "usage": {
                "prompt_tokens": 0,
                "completion_tokens": approx_tokens,
                "total_tokens": approx_tokens,
            },
        }
    )


# ---------------------------------------------------------------------------
#  AI 對話（簡易 JSON API，保留給自訂客戶端）
# ---------------------------------------------------------------------------
@app.post("/ai/chat")
async def ai_chat(
    user_message: str,
    history: list[dict[str, str]] | None = None,
) -> JSONResponse:
    if not user_message or not user_message.strip():
        raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="使用者訊息不能為空")

    messages: list[dict[str, str]] = []
    if history:
        for msg in history:
            if msg.get("role") and msg.get("content"):
                messages.append({"role": msg["role"], "content": msg["content"]})
    messages.append({"role": "user", "content": user_message})

    try:
        reply = _reply_of(messages)
    except ValueError as exc:
        raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail=str(exc)) from exc
    except Exception as exc:
        logger.exception("AI chat failed")
        raise HTTPException(status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
                            detail="AI 語意理解失敗") from exc

    return JSONResponse(content={"text": reply})


# ---------------------------------------------------------------------------
#  語音 + AI 一次完成
# ---------------------------------------------------------------------------
@app.post("/voice/chat")
@app.post("/v1/voice/chat")
async def voice_chat(
    file: UploadFile = File(...),
    language: str | None = Form(None),
) -> JSONResponse:
    start = time.perf_counter()

    raw = await file.read()
    if not raw:
        raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="空的音檔")

    whisper = state.whisper
    ai = state.ai

    if whisper is None:
        raise HTTPException(status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
                            detail="Whisper 模型尚未載入")
    if ai is None:
        raise HTTPException(status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
                            detail="AI 客戶端尚未初始化")

    try:
        transcript = await transcribe_wav_bytes(
            whisper=whisper,
            audio_bytes=raw,
            model_hint=None,
            language_hint=language,
        )
    except Exception as exc:
        logger.exception("STT failed")
        raise HTTPException(status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
                            detail="語音辨識失敗") from exc

    if not transcript:
        raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST,
                            detail="沒有辨識到語音內容")

    try:
        reply = _reply_of([{"role": "user", "content": transcript}])
    except Exception as exc:
        logger.exception("AI chat failed")
        raise HTTPException(status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
                            detail="AI 語意理解失敗") from exc

    elapsed_ms = int((time.perf_counter() - start) * 1000)
    logger.info(
        "voice_chat: transcript=%s reply_len=%d elapsed_ms=%d",
        transcript[:50], len(reply), elapsed_ms,
    )

    return JSONResponse(content={
        "transcript": transcript,
        "reply": reply,
        "elapsed_ms": elapsed_ms,
    })


def run() -> None:
    host = os.environ.get("STT_HOST", "0.0.0.0")
    port = int(os.environ.get("STT_PORT", "8000"))
    log_level = os.environ.get("LOG_LEVEL", "info").upper()
    uvicorn.run(
        "app:app",
        host=host,
        port=port,
        log_level=log_level,
        access_log=True,
        factory=False,
    )


if __name__ == "__main__":
    logging.basicConfig(level=os.environ.get("LOG_LEVEL", "INFO").upper())
    run()