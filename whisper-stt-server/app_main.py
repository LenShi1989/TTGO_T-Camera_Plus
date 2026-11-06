import logging

logger = logging.getLogger("whisper-stt")


def main() -> None:
    logging.basicConfig(level=logging.INFO)
    logger.info("start whisper-stt-server")
    import uvicorn

    uvicorn.run(
        "app:app",
        host="0.0.0.0",
        port=8000,
        log_level="info",
        access_log=True,
    )


if __name__ == "__main__":
    main()
