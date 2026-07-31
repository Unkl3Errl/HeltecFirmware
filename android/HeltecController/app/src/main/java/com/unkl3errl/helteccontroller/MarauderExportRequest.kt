package com.unkl3errl.helteccontroller

data class MarauderExportRequest(
    val suggestedName: String,
    val mimeType: String,
    val content: ByteArray,
)
