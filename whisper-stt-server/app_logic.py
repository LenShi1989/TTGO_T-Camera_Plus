import logging
from typing import Any

logger = logging.getLogger("whisper-stt")


async def transcribe_wav_bytes(
    whisper: Any,
    audio_bytes: bytes,
    model_hint: str | None = None,
    language_hint: str | None = None,
) -> str:
    """將 WAV 位元組轉為文字。"""
    import asyncio

    loop = asyncio.get_running_loop()
    return await loop.run_in_executor(
        None,
        _sync_transcribe,
        whisper,
        audio_bytes,
        model_hint,
        language_hint,
    )


def _sync_transcribe(
    whisper: Any,
    audio_bytes: bytes,
    model_hint: str | None,
    language_hint: str | None,
) -> str:
    # faster-whisper / PyAV 需要檔案物件或路徑，直接丟 bytes 會出現
    # "File object has no read() method"。用 BytesIO 包起來。
    import io

    segments, info = whisper.transcribe(
        io.BytesIO(audio_bytes),
        language=language_hint,
        task="transcribe",
        beam_size=5,
        vad_filter=False,
    )

    pieces: list[str] = []
    for segment in segments:
        text = segment.text
        if text:
            pieces.append(text)

    merged = "".join(pieces).strip()
    if not merged:
        logger.warning("whisper 回傳空字串 model_hint=%s language_hint=%s", model_hint, language_hint)

    return merged
