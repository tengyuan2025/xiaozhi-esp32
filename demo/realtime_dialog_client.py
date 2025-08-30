import websockets
import gzip
import json

from typing import Dict, Any

import protocol
import config


class RealtimeDialogClient:
    def __init__(self, config: Dict[str, Any], session_id: str, output_audio_format: str = "pcm") -> None:
        self.config = config
        self.logid = ""
        self.session_id = session_id
        self.output_audio_format = output_audio_format
        self.ws = None

    async def connect(self) -> None:
        """建立WebSocket连接"""
        print(f"url: {self.config['base_url']}, headers: {self.config['headers']}")
        self.ws = await websockets.connect(
            self.config['base_url'],
            extra_headers=self.config['headers'],
            ping_interval=None
        )
        self.logid = self.ws.response_headers.get("X-Tt-Logid")
        print(f"dialog server response logid: {self.logid}")

        # StartConnection request
        start_connection_request = bytearray(protocol.generate_header())
        start_connection_request.extend(int(1).to_bytes(4, 'big'))
        payload_bytes = str.encode("{}")
        payload_bytes = gzip.compress(payload_bytes)
        start_connection_request.extend((len(payload_bytes)).to_bytes(4, 'big'))
        start_connection_request.extend(payload_bytes)
        await self.ws.send(start_connection_request)
        response = await self.ws.recv()
        print(f"StartConnection response: {protocol.parse_response(response)}")

        # StartSession request
        if self.output_audio_format == "pcm_s16le":
            config.start_session_req["tts"]["audio_config"]["format"] = "pcm_s16le"
        request_params = config.start_session_req
        payload_bytes = str.encode(json.dumps(request_params))
        payload_bytes = gzip.compress(payload_bytes)
        start_session_request = bytearray(protocol.generate_header())
        start_session_request.extend(int(100).to_bytes(4, 'big'))
        start_session_request.extend((len(self.session_id)).to_bytes(4, 'big'))
        start_session_request.extend(str.encode(self.session_id))
        start_session_request.extend((len(payload_bytes)).to_bytes(4, 'big'))
        start_session_request.extend(payload_bytes)
        await self.ws.send(start_session_request)
        response = await self.ws.recv()
        print(f"StartSession response: {protocol.parse_response(response)}")

    async def say_hello(self) -> None:
        """发送Hello消息"""
        payload = {
            "content": "你好，我是豆包，有什么可以帮助你的？",
        }
        hello_request = bytearray(protocol.generate_header())
        hello_request.extend(int(300).to_bytes(4, 'big'))
        payload_bytes = str.encode(json.dumps(payload))
        payload_bytes = gzip.compress(payload_bytes)
        hello_request.extend((len(self.session_id)).to_bytes(4, 'big'))
        hello_request.extend(str.encode(self.session_id))
        hello_request.extend((len(payload_bytes)).to_bytes(4, 'big'))
        hello_request.extend(payload_bytes)
        await self.ws.send(hello_request)
    async def chat_text_query(self, content: str) -> None:
        """发送Chat Text Query消息"""
        payload = {
            "content": content,
        }
        chat_text_query_request = bytearray(protocol.generate_header())
        chat_text_query_request.extend(int(501).to_bytes(4, 'big'))
        payload_bytes = str.encode(json.dumps(payload))
        payload_bytes = gzip.compress(payload_bytes)
        chat_text_query_request.extend((len(self.session_id)).to_bytes(4, 'big'))
        chat_text_query_request.extend(str.encode(self.session_id))
        chat_text_query_request.extend((len(payload_bytes)).to_bytes(4, 'big'))
        chat_text_query_request.extend(payload_bytes)
        await self.ws.send(chat_text_query_request)


    async def chat_tts_text(self, is_user_querying: bool, start: bool, end: bool, content: str) -> None:
        if is_user_querying:
            return
        """发送Chat TTS Text消息"""
        payload = {
            "start": start,
            "end": end,
            "content": content,
        }
        print(f"ChatTTSTextRequest payload: {payload}")
        payload_bytes = str.encode(json.dumps(payload))
        payload_bytes = gzip.compress(payload_bytes)

        chat_tts_text_request = bytearray(protocol.generate_header())
        chat_tts_text_request.extend(int(500).to_bytes(4, 'big'))
        chat_tts_text_request.extend((len(self.session_id)).to_bytes(4, 'big'))
        chat_tts_text_request.extend(str.encode(self.session_id))
        chat_tts_text_request.extend((len(payload_bytes)).to_bytes(4, 'big'))
        chat_tts_text_request.extend(payload_bytes)
        await self.ws.send(chat_tts_text_request)

    async def task_request(self, audio: bytes) -> None:
        task_request = bytearray(
            protocol.generate_header(message_type=protocol.CLIENT_AUDIO_ONLY_REQUEST,
                                     serial_method=protocol.NO_SERIALIZATION))
        task_request.extend(int(200).to_bytes(4, 'big'))
        task_request.extend((len(self.session_id)).to_bytes(4, 'big'))
        task_request.extend(str.encode(self.session_id))
        payload_bytes = gzip.compress(audio)
        task_request.extend((len(payload_bytes)).to_bytes(4, 'big'))  # payload size(4 bytes)
        task_request.extend(payload_bytes)
        await self.ws.send(task_request)

    async def receive_server_response(self) -> Dict[str, Any]:
        try:
            response = await self.ws.recv()
            data = protocol.parse_response(response)
            return data
        except Exception as e:
            raise Exception(f"Failed to receive message: {e}")

    async def finish_session(self):
        finish_session_request = bytearray(protocol.generate_header())
        finish_session_request.extend(int(102).to_bytes(4, 'big'))
        payload_bytes = str.encode("{}")
        payload_bytes = gzip.compress(payload_bytes)
        finish_session_request.extend((len(self.session_id)).to_bytes(4, 'big'))
        finish_session_request.extend(str.encode(self.session_id))
        finish_session_request.extend((len(payload_bytes)).to_bytes(4, 'big'))
        finish_session_request.extend(payload_bytes)
        await self.ws.send(finish_session_request)

    async def finish_connection(self):
        finish_connection_request = bytearray(protocol.generate_header())
        finish_connection_request.extend(int(2).to_bytes(4, 'big'))
        payload_bytes = str.encode("{}")
        payload_bytes = gzip.compress(payload_bytes)
        finish_connection_request.extend((len(payload_bytes)).to_bytes(4, 'big'))
        finish_connection_request.extend(payload_bytes)
        await self.ws.send(finish_connection_request)
        response = await self.ws.recv()
        print(f"FinishConnection response: {protocol.parse_response(response)}")

    async def close(self) -> None:
        """关闭WebSocket连接"""
        if self.ws:
            print(f"Closing WebSocket connection...")
            await self.ws.close()
