import logging
import os

logger = logging.getLogger("whisper-stt")


def main() -> None:
    logging.basicConfig(level=os.environ.get("LOG_LEVEL", "INFO").upper())
    logger.info("start whisper-stt-server")

    # app.run() 會讀取 STT_HOST / STT_PORT / LOG_LEVEL 環境變數，
    # 這裡不要另外硬編 8000，否則 README 的設定會失效。
    from app import run

    run()


if __name__ == "__main__":
    main()