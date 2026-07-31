package com.unkl3errl.helteccontroller.marauder

import android.annotation.SuppressLint
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import android.os.Build
import com.hoho.android.usbserial.driver.CdcAcmSerialDriver
import com.hoho.android.usbserial.driver.UsbSerialDriver
import com.hoho.android.usbserial.driver.UsbSerialPort
import com.hoho.android.usbserial.driver.UsbSerialProber
import com.hoho.android.usbserial.util.SerialInputOutputManager
import java.util.concurrent.Executors

class MarauderUsbSerial(
    private val context: Context,
    private val listener: Listener,
) : SerialInputOutputManager.Listener {
    interface Listener {
        fun onSerialStatus(message: String, connected: Boolean)
        fun onSerialData(data: ByteArray)
        fun onSerialError(message: String)
    }

    companion object {
        private const val ACTION_USB_PERMISSION =
            "com.unkl3errl.helteccontroller.USB_PERMISSION"
        private const val ESPRESSIF_VID = 0x303A
        private const val ESP32_USB_JTAG_PID = 0x1001
    }

    private val usbManager = context.getSystemService(Context.USB_SERVICE) as UsbManager
    private val writer = Executors.newSingleThreadExecutor()
    private var port: UsbSerialPort? = null
    private var ioManager: SerialInputOutputManager? = null
    private var pendingDriver: UsbSerialDriver? = null
    private var currentDeviceId: Int? = null

    val isConnected: Boolean
        get() = port?.isOpen == true

    private val receiver = object : BroadcastReceiver() {
        override fun onReceive(receiverContext: Context, intent: Intent) {
            when (intent.action) {
                ACTION_USB_PERMISSION -> {
                    val device = intent.usbDevice()
                    val granted = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)
                    val driver = pendingDriver
                    pendingDriver = null
                    if (granted && device != null && driver != null) open(driver)
                    else listener.onSerialStatus("USB permission was not granted", false)
                }
                UsbManager.ACTION_USB_DEVICE_DETACHED -> {
                    val device = intent.usbDevice()
                    if (device?.deviceId == currentDeviceId) {
                        close()
                        listener.onSerialStatus("Marauder USB device disconnected", false)
                    }
                }
            }
        }
    }

    init {
        registerUsbReceiver()
    }

    @SuppressLint("UnspecifiedRegisterReceiverFlag")
    private fun registerUsbReceiver() {
        val filter = IntentFilter().apply {
            addAction(ACTION_USB_PERMISSION)
            addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
        }
        if (Build.VERSION.SDK_INT >= 33) {
            context.registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED)
        } else {
            @Suppress("DEPRECATION")
            context.registerReceiver(receiver, filter)
        }
    }

    fun connect() {
        if (isConnected) {
            listener.onSerialStatus("Marauder USB is already connected", true)
            return
        }
        val driver = findDriver()
        if (driver == null) {
            listener.onSerialStatus(
                "No compatible USB serial device found. Check the OTG adapter and cable.",
                false,
            )
            return
        }
        if (!usbManager.hasPermission(driver.device)) {
            pendingDriver = driver
            val permissionIntent = PendingIntent.getBroadcast(
                context,
                0,
                Intent(ACTION_USB_PERMISSION).setPackage(context.packageName),
                PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
            )
            usbManager.requestPermission(driver.device, permissionIntent)
            listener.onSerialStatus("Waiting for USB permission…", false)
            return
        }
        open(driver)
    }

    fun writeCommand(command: String) {
        val activePort = port
        if (activePort == null || !activePort.isOpen) {
            listener.onSerialError("Connect the Marauder USB device first")
            return
        }
        writer.execute {
            try {
                activePort.write((command.trim() + "\r\n").toByteArray(Charsets.UTF_8), 2_000)
            } catch (error: Exception) {
                listener.onSerialError("USB write failed: ${error.message ?: error.javaClass.simpleName}")
            }
        }
    }

    fun close() {
        ioManager?.stop()
        ioManager = null
        runCatching { port?.close() }
        port = null
        currentDeviceId = null
    }

    fun destroy() {
        close()
        runCatching { context.unregisterReceiver(receiver) }
        writer.shutdownNow()
    }

    override fun onNewData(data: ByteArray) = listener.onSerialData(data)

    override fun onRunError(error: Exception) {
        close()
        listener.onSerialError("USB serial stopped: ${error.message ?: error.javaClass.simpleName}")
        listener.onSerialStatus("Marauder USB disconnected", false)
    }

    private fun findDriver(): UsbSerialDriver? {
        val detected = UsbSerialProber.getDefaultProber().findAllDrivers(usbManager)
        detected.firstOrNull { driver ->
            driver.device.vendorId == ESPRESSIF_VID && driver.device.productId == ESP32_USB_JTAG_PID
        }?.let { return it }
        detected.firstOrNull()?.let { return it }

        val nativeEspressif = usbManager.deviceList.values.firstOrNull {
            it.vendorId == ESPRESSIF_VID && it.productId == ESP32_USB_JTAG_PID
        }
        return nativeEspressif?.let(::CdcAcmSerialDriver)
    }

    private fun open(driver: UsbSerialDriver) {
        try {
            val connection = usbManager.openDevice(driver.device)
                ?: throw IllegalStateException("Android could not open the USB device")
            val selectedPort = driver.ports.firstOrNull()
                ?: throw IllegalStateException("The USB device has no serial port")
            selectedPort.open(connection)
            selectedPort.setParameters(
                115_200,
                8,
                UsbSerialPort.STOPBITS_1,
                UsbSerialPort.PARITY_NONE,
            )
            port = selectedPort
            currentDeviceId = driver.device.deviceId
            ioManager = SerialInputOutputManager(selectedPort, this).also { it.start() }
            listener.onSerialStatus(
                "Connected: ${driver.device.productName ?: "ESP32-S3"} · 115200 baud",
                true,
            )
        } catch (error: Exception) {
            close()
            listener.onSerialError("USB connection failed: ${error.message ?: error.javaClass.simpleName}")
            listener.onSerialStatus("Marauder USB connection failed", false)
        }
    }

    @Suppress("DEPRECATION")
    private fun Intent.usbDevice(): UsbDevice? =
        if (Build.VERSION.SDK_INT >= 33) {
            getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
        } else {
            getParcelableExtra(UsbManager.EXTRA_DEVICE)
        }
}
