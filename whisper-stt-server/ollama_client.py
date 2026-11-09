import logging
from typing import Any

logger = logging.getLogger("whisper-stt")


class OllamaClient:
    """與 Ollama 服務器通信，用於 Qwen 等模型的語意理解。"""

    def __init__(self, base_url: str, model: str, api_key: str = ""):
        self.base_url = base_url.rstrip("/")
        self.model = model
        self.api_key = api_key
        self._client = None

    def _get_client(self):
        import httpx

        if self._client is None:
            timeout = httpx.Timeout(45.0, connect=10.0)
            self._client = httpx.Client(base_url=self.base_url, timeout=timeout)
        return self._client

    def chat(self, messages: list[dict[str, str]], temperature: float = 0.7,
             max_tokens: int = 512, system_prompt: str = "") -> str:
        """調用 Ollama chat API，返回模型回應文本。"""
        import httpx

        client = self._get_client()

        payload: dict[str, Any] = {
            "model": self.model,
            "messages": messages,
            "stream": False,
            "options": {
                "temperature": temperature,
                "num_predict": max_tokens,
            },
            "think": False,
        }

        if system_prompt:
            payload["system"] = system_prompt

        try:
            response = client.post("/api/chat", json=payload)
            response.raise_for_status()
            data = response.json()

            if "message" in data and "content" in data["message"]:
                return data["message"]["content"]

            logger.warning("Ollama 回應格式異常: %s", data)
            return ""

        except httpx.HTTPStatusError as e:
            logger.error("Ollama API 錯誤 %d: %s", e.response.status_code, e.response.text)
            raise
        except Exception as e:
            logger.exception("Ollama 請求失敗")
            raise

    def close(self):
        if self._client:
            self._client.close()
            self._client = None


class AIClient:
    """統一的 AI 客戶端接口，支持 Ollama 和 OpenAI 兼容 API。"""

    def __init__(self, mode: str, base_url: str, api_key: str, model: str,
                 system_prompt: str, temperature: float, max_tokens: int):
        self.mode = mode
        self.base_url = base_url.rstrip("/")
        self.api_key = api_key
        self.model = model
        self.system_prompt = system_prompt
        self.temperature = temperature
        self.max_tokens = max_tokens
        self.ollama: OllamaClient | None = None
        self._client = None

    def _get_http_client(self):
        import httpx

        if self._client is None:
            timeout = httpx.Timeout(45.0, connect=10.0)
            headers = {"Authorization": f"Bearer {self.api_key}"} if self.api_key else {}
            self._client = httpx.Client(base_url=self.base_url, timeout=timeout, headers=headers)
        return self._client

    def chat(self, messages: list[dict[str, str]], temperature: float | None = None,
             max_tokens: int | None = None) -> str:
        """送出對話。messages 已含 system 時不會重複插入系統提示詞。"""
        msgs = list(messages)
        if self.system_prompt and not any(m.get("role") == "system" for m in msgs):
            msgs.insert(0, {"role": "system", "content": self.system_prompt})

        temp = self.temperature if temperature is None else float(temperature)
        tokens = self.max_tokens if max_tokens is None else int(max_tokens)

        if self.mode == "ollama":
            if not self.ollama:
                self.ollama = OllamaClient(
                    base_url=self.base_url.replace("/api/chat", ""),
                    model=self.model,
                )
            return self.ollama.chat(
                messages=msgs,
                temperature=temp,
                max_tokens=tokens,
                system_prompt="",
            )
        else:
            import httpx

            client = self._get_http_client()
            payload = {
                "model": self.model,
                "messages": msgs,
                "temperature": temp,
                "max_tokens": tokens,
                "stream": False,
            }

            response = client.post("/chat/completions", json=payload)
            response.raise_for_status()
            data = response.json()

            choices = data.get("choices", [])
            if choices and "message" in choices[0]:
                return choices[0]["message"]["content"]

            logger.warning("AI 回應格式異常: %s", data)
            return ""

    def close(self):
        if self._client:
            self._client.close()
            self._client = None
        if self.ollama:
            self.ollama.close()
            self.ollama = None