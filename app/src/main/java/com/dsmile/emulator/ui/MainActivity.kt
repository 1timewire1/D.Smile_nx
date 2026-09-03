package com.dsmile.emulator.ui

import android.app.Activity
import android.app.AlertDialog
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.view.Gravity
import android.view.ViewGroup
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ListView
import android.widget.TextView
import android.widget.Toast
import androidx.documentfile.provider.DocumentFile

class MainActivity : Activity() {

    private data class RomEntry(val name: String, val uri: Uri)

    private val prefs by lazy { getSharedPreferences("dsmile", Context.MODE_PRIVATE) }
    private val roms = ArrayList<RomEntry>()
    private lateinit var adapter: ArrayAdapter<String>
    private lateinit var statusText: TextView

    private companion object {
        const val REQ_PICK_FOLDER = 1
        const val REQ_PICK_BIOS = 2
        val ROM_EXTENSIONS = setOf("bin", "rom", "vsmile")
    }

    private val biosFile get() = java.io.File(filesDir, "sysrom.bin")

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val pad = (resources.displayMetrics.density * 16).toInt()
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(pad, pad, pad, pad)
        }
        val title = TextView(this).apply {
            text = "D.Smile"
            textSize = 28f
            gravity = Gravity.CENTER_HORIZONTAL
        }
        val versionText = TextView(this).apply {
            text = try { "v" + packageManager.getPackageInfo(packageName, 0).versionName } catch (e: Exception) { "" }
            textSize = 12f
            alpha = 0.6f
            gravity = Gravity.CENTER_HORIZONTAL
            setPadding(0, 0, 0, pad / 2)
        }
        statusText = TextView(this).apply {
            textSize = 13f
            gravity = Gravity.CENTER_HORIZONTAL
            setPadding(0, 0, 0, pad / 2)
        }
        val buttonRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        val pickBtn = Button(this).apply {
            text = "ROM folder…"
            setOnClickListener { pickFolder() }
        }
        val biosBtn = Button(this).apply {
            text = "BIOS…"
            setOnClickListener { biosOptions() }
        }
        val settingsBtn = Button(this).apply {
            text = "Settings"
            setOnClickListener { showSettings() }
        }
        buttonRow.addView(pickBtn, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        buttonRow.addView(biosBtn, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        buttonRow.addView(settingsBtn, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))

        adapter = ArrayAdapter(this, android.R.layout.simple_list_item_1)
        val list = ListView(this).apply {
            adapter = this@MainActivity.adapter
            setOnItemClickListener { _, _, pos, _ -> launchRom(roms[pos]) }
        }

        root.addView(title)
        root.addView(versionText)
        root.addView(statusText)
        root.addView(buttonRow)
        root.addView(list, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))
        setContentView(root)

        scanRoms()
        checkForUpdate(root)
    }

    // ---------------- update check ----------------

    /** One anonymous GitHub API call; on a newer release, shows a tappable
     *  banner under the version line that opens the release page. Silent on
     *  any failure (offline, rate limit) so the screen never blocks on it. */
    private fun checkForUpdate(root: LinearLayout) {
        Thread {
            try {
                val conn = java.net.URL(
                    "https://api.github.com/repos/derik-dot-digital/D.Smile/releases/latest"
                ).openConnection() as java.net.HttpURLConnection
                conn.setRequestProperty("Accept", "application/vnd.github+json")
                conn.setRequestProperty("User-Agent", "D.Smile")
                conn.connectTimeout = 6000
                conn.readTimeout = 6000
                val body = conn.inputStream.use { it.readBytes().decodeToString() }
                conn.disconnect()
                val json = org.json.JSONObject(body)
                val remote = json.optString("tag_name").removePrefix("v")
                val pageUrl = json.optString("html_url")
                val local = packageManager.getPackageInfo(packageName, 0).versionName ?: return@Thread
                if (pageUrl.isEmpty() || !isNewerVersion(remote, local)) return@Thread
                runOnUiThread {
                    if (isFinishing || isDestroyed) return@runOnUiThread
                    val pad = (resources.displayMetrics.density * 8).toInt()
                    val banner = TextView(this).apply {
                        text = "Update available: v$remote — tap to get it"
                        textSize = 14f
                        gravity = Gravity.CENTER_HORIZONTAL
                        setTextColor(0xFF69F0AE.toInt())
                        setPadding(pad, pad, pad, pad)
                        setOnClickListener {
                            try {
                                startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(pageUrl)))
                            } catch (e: Exception) {
                                Toast.makeText(this@MainActivity, pageUrl, Toast.LENGTH_LONG).show()
                            }
                        }
                    }
                    root.addView(banner, root.indexOfChild(statusText))
                }
            } catch (e: Exception) {
                // No network, rate-limited, or GitHub unreachable: say nothing.
            }
        }.start()
    }

    private fun isNewerVersion(remote: String, local: String): Boolean {
        if (remote.isEmpty()) return false
        val r = remote.split('.').map { it.toIntOrNull() ?: return false }
        val l = local.split('.').map { it.toIntOrNull() ?: return false }
        for (i in 0 until maxOf(r.size, l.size)) {
            val a = r.getOrElse(i) { 0 }
            val b = l.getOrElse(i) { 0 }
            if (a != b) return a > b
        }
        return false
    }

    private fun pickFolder() {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT_TREE).addFlags(
            Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION
        )
        startActivityForResult(intent, REQ_PICK_FOLDER)
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == REQ_PICK_FOLDER && resultCode == RESULT_OK) {
            val uri = data?.data ?: return
            contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
            prefs.edit().putString("romFolder", uri.toString()).apply()
            scanRoms()
        }
        if (requestCode == REQ_PICK_BIOS && resultCode == RESULT_OK) {
            val uri = data?.data ?: return
            importBios(uri)
        }
    }

    // ---------------- BIOS import ----------------

    private fun biosOptions() {
        if (!biosFile.exists()) {
            AlertDialog.Builder(this)
                .setTitle("BIOS (optional)")
                .setMessage(
                    "A BIOS is NOT required - games boot and play without one.\n\n" +
                    "Importing a real V.Smile sysrom dump improves compatibility with " +
                    "games that call built-in console routines. You can add or remove " +
                    "one at any time."
                )
                .setPositiveButton("Import…") { _, _ -> pickBiosFile() }
                .setNegativeButton("Not now", null)
                .show()
            return
        }
        AlertDialog.Builder(this)
            .setTitle("BIOS (imported)")
            .setItems(arrayOf("Replace…", "Remove")) { _, i ->
                when (i) {
                    0 -> pickBiosFile()
                    1 -> {
                        biosFile.delete()
                        Toast.makeText(this, "BIOS removed", Toast.LENGTH_SHORT).show()
                        scanRoms()
                    }
                }
            }
            .show()
    }

    private fun pickBiosFile() {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "*/*"
        }
        startActivityForResult(intent, REQ_PICK_BIOS)
    }

    private fun importBios(uri: Uri) {
        val bytes = try {
            contentResolver.openInputStream(uri)?.use { it.readBytes() }
        } catch (e: Exception) { null }
        if (bytes == null) {
            Toast.makeText(this, "Could not read that file", Toast.LENGTH_LONG).show()
            return
        }
        val (valid, swapped) = validateBios(bytes)
        if (valid == null) {
            Toast.makeText(
                this,
                "Not a valid V.Smile BIOS (expected a 2 MiB sysrom dump with sane boot vectors)",
                Toast.LENGTH_LONG
            ).show()
            return
        }
        biosFile.writeBytes(valid)
        val note = if (swapped) " (byte order corrected)" else ""
        Toast.makeText(this, "BIOS imported$note", Toast.LENGTH_LONG).show()
        scanRoms()
    }

    /** Validates a sysrom dump: correct size, and the interrupt vector table at
     *  words 0xFFF5-0xFFFF must decode to plausible code addresses. Accepts
     *  either byte order and returns (fixed-LE data or null, wasByteSwapped). */
    private fun validateBios(data: ByteArray): Pair<ByteArray?, Boolean> {
        if (data.size != 0x200000) return null to false
        fun score(swap: Boolean): Int {
            var ok = 0
            for (w in 0xFFF5..0xFFFF) {
                val i = w * 2
                val lo = if (swap) data[i + 1] else data[i]
                val hi = if (swap) data[i] else data[i + 1]
                val v = (lo.toInt() and 0xFF) or ((hi.toInt() and 0xFF) shl 8)
                if (v in 0x0100..0xFFF0) ok++
            }
            return ok
        }
        val le = score(false)
        val be = score(true)
        return when {
            le >= 9 && le >= be -> data to false
            be >= 9 -> ByteArray(data.size) { i -> if (i % 2 == 0) data[i + 1] else data[i - 1] } to true
            else -> null to false
        }
    }

    private fun scanRoms() {
        roms.clear()
        adapter.clear()
        val folderStr = prefs.getString("romFolder", null)
        if (folderStr == null) {
            statusText.text = "Pick your ROM folder to get started"
            return
        }
        val dir = DocumentFile.fromTreeUri(this, Uri.parse(folderStr))
        if (dir == null || !dir.isDirectory) {
            statusText.text = "ROM folder unavailable — pick it again"
            return
        }
        var sysromUri: String? = null
        for (f in dir.listFiles()) {
            val name = f.name ?: continue
            val ext = name.substringAfterLast('.', "").lowercase()
            if (ext !in ROM_EXTENSIONS) continue
            val base = name.lowercase()
            if ("bios" in base || "sysrom" in base || "system rom" in base || base.startsWith("vsmile_v")) {
                sysromUri = f.uri.toString()
                continue
            }
            roms.add(RomEntry(name.removeSuffix(".$ext").removeSuffix(".${ext.uppercase()}"), f.uri))
        }
        prefs.edit().putString("sysromUri", sysromUri).apply()
        roms.sortBy { it.name.lowercase() }
        for (r in roms) adapter.add(r.name)
        val bios = when { biosFile.exists() -> " • BIOS imported"; sysromUri != null -> " • BIOS found in folder"; else -> " • no BIOS (optional)" }
        statusText.text = "${roms.size} game(s)$bios"
        if (roms.isEmpty()) {
            Toast.makeText(this, "No .bin ROMs found in that folder", Toast.LENGTH_LONG).show()
        }
    }

    private fun launchRom(rom: RomEntry) {
        startActivity(
            Intent(this, EmuActivity::class.java)
                .setAction(Intent.ACTION_VIEW)
                .setData(rom.uri)
                .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        )
    }

    private fun showSettings() {
        val pal = prefs.getBoolean("pal", false)
        val intro = prefs.getBoolean("playIntro", true)
        val items = arrayOf(
            "Video system: ${if (pal) "PAL (50 Hz)" else "NTSC (60 Hz)"}",
            "Game intros: ${if (intro) "on" else "skipped (fast boot)"}",
            "All files access (for frontends like iiSU)…"
        )
        AlertDialog.Builder(this)
            .setTitle("Settings")
            .setItems(items) { _, which ->
                when (which) {
                    0 -> { prefs.edit().putBoolean("pal", !pal).apply(); showSettings() }
                    1 -> { prefs.edit().putBoolean("playIntro", !intro).apply(); showSettings() }
                    2 -> requestAllFilesAccess()
                }
            }
            .show()
    }

    private fun requestAllFilesAccess() {
        if (android.os.Build.VERSION.SDK_INT >= 30) {
            if (android.os.Environment.isExternalStorageManager()) {
                Toast.makeText(this, "Already granted", Toast.LENGTH_SHORT).show()
                return
            }
            startActivity(
                Intent(
                    android.provider.Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                    Uri.parse("package:$packageName")
                )
            )
        }
    }

    override fun onResume() {
        super.onResume()
        if (prefs.getString("romFolder", null) != null) scanRoms()
    }
}
