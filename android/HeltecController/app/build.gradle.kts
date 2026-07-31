plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

val releaseStoreFile = providers.environmentVariable("HELTEC_RELEASE_STORE_FILE")
val releaseStorePassword = providers.environmentVariable("HELTEC_RELEASE_STORE_PASSWORD")
val releaseKeyAlias = providers.environmentVariable("HELTEC_RELEASE_KEY_ALIAS")
val releaseKeyPassword = providers.environmentVariable("HELTEC_RELEASE_KEY_PASSWORD")
val releaseSigningValues = listOf(
    releaseStoreFile,
    releaseStorePassword,
    releaseKeyAlias,
    releaseKeyPassword,
)
val releaseSigningConfigured = releaseSigningValues.all { it.isPresent }

if (!releaseSigningConfigured && releaseSigningValues.any { it.isPresent }) {
    throw GradleException(
        "Release signing is partially configured. Set all four HELTEC_RELEASE_* variables."
    )
}

android {
    namespace = "com.unkl3errl.helteccontroller"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.unkl3errl.helteccontroller"
        minSdk = 29
        targetSdk = 35
        versionCode = 4
        versionName = "0.3.1"

        testInstrumentationRunner = "android.test.InstrumentationTestRunner"
    }

    signingConfigs {
        if (releaseSigningConfigured) {
            create("release") {
                storeFile = file(releaseStoreFile.get())
                storePassword = releaseStorePassword.get()
                keyAlias = releaseKeyAlias.get()
                keyPassword = releaseKeyPassword.get()
            }
        }
    }

    buildTypes {
        release {
            if (releaseSigningConfigured) {
                signingConfig = signingConfigs.getByName("release")
            }
            isMinifyEnabled = true
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }
}

dependencies {
    implementation("com.github.mik3y:usb-serial-for-android:3.10.0")
    testImplementation("junit:junit:4.13.2")
}
