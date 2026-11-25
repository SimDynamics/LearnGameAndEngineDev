package lgaed.narration.windows

import java.io.File
import java.io.IOException
import java.nio.file.FileSystems
import java.nio.file.Files
import java.nio.file.LinkOption.NOFOLLOW_LINKS
import java.nio.file.NoSuchFileException
import java.nio.file.Path
import java.nio.file.StandardWatchEventKinds.ENTRY_CREATE
import java.nio.file.StandardWatchEventKinds.ENTRY_DELETE
import java.nio.file.StandardWatchEventKinds.ENTRY_MODIFY
import java.nio.file.StandardWatchEventKinds.OVERFLOW
import java.nio.file.WatchKey
import java.nio.file.WatchService
import java.nio.file.attribute.BasicFileAttributes
import java.nio.file.attribute.FileTime

/** One flat PCM/Opus cache, observed before its first inventory is read.
 * Events refresh the current entry rather than adding event-sized deltas, so
 * delayed/duplicate notifications cannot double-count our own publications.
 * A broken watcher, replacement directory or overflow discards the inventory.
 * Failure to rebuild fails a new recording closed; existing audio is untouched.
 */
internal class WindowsAudioStorageLedger(
    private val directory: Path,
    private val freeBytes: () -> Long = { Files.getFileStore(directory).usableSpace }
) : AutoCloseable {
    data class Snapshot(val used: Long, val free: Long, val limit: Long)
    private data class DirectoryIdentity(val key: Any?, val created: FileTime)
    private var watcher: WatchService? = null
    private var registration: WatchKey? = null
    private var identity: DirectoryIdentity? = null
    // Path keys use the native provider's case rules, including Windows aliases.
    private val sizes = HashMap<Path,Long>()
    private var total = 0L
    private var dirty = true
    private var closed = false
    var scans = 0L
        private set

    private fun identity(): DirectoryIdentity {
        val attributes = Files.readAttributes(directory,BasicFileAttributes::class.java,NOFOLLOW_LINKS)
        check(attributes.isDirectory && !attributes.isSymbolicLink) { "Narration cache is no longer a private directory" }
        return DirectoryIdentity(attributes.fileKey(),attributes.creationTime())
    }
    private fun audioName(name: String) = name.endsWith(".wav") || name.endsWith(".ogg")
    private fun size(name: String): Long? {
        if (!audioName(name)) return null
        val attributes = try { Files.readAttributes(directory.resolve(name),BasicFileAttributes::class.java,NOFOLLOW_LINKS) }
            catch (_: NoSuchFileException) { return null }
        check(!attributes.isSymbolicLink) { "Narration cache cannot inventory audio links" }
        return if (attributes.isRegularFile) attributes.size() else null
    }
    private fun update(name: String) {
        if (!audioName(name)) return
        val entry = directory.resolve(name)
        val previous = sizes[entry] ?: 0L
        val current = size(name)
        total = Math.addExact(total - previous,current ?: 0L)
        if (current == null) sizes.remove(entry) else sizes[entry] = current
    }
    private fun discardWatcher() {
        registration?.cancel();registration=null
        watcher?.close();watcher=null
    }
    private fun rebuild(checkCancellation: () -> Unit) {
        discardWatcher();dirty=true
        checkCancellation()
        val before = identity()
        try {
            val next = FileSystems.getDefault().newWatchService();watcher=next
            registration = directory.register(next,ENTRY_CREATE,ENTRY_DELETE,ENTRY_MODIFY)
            if (identity() != before) return
            // Register first: mutations during enumeration are replayed below.
            val replacement = HashMap<Path,Long>();var used=0L
            Files.newDirectoryStream(directory).use { entries ->
                scans++
                var visited=0
                for (entry in entries) {
                    if(visited++%128==0)checkCancellation()
                    val name=entry.fileName.toString();val bytes=size(name) ?: continue
                    val previous=replacement.put(directory.resolve(name),bytes) ?: 0L
                    used=Math.addExact(used-previous,bytes)
                }
            }
            checkCancellation();if (identity() != before) return
            sizes.clear();sizes.putAll(replacement);total=used;identity=before;dirty=false
        } catch (failure: Throwable) {
            discardWatcher();throw failure
        }
    }
    /** Same recovery entry point used for a native OVERFLOW notification.
     * Deterministic tests invoke it without fabricating a massive OS overflow. */
    @Synchronized internal fun invalidateForOverflow() { dirty=true }

    private fun drain(checkCancellation: () -> Unit) {
        val service = watcher ?: return
        repeat(512) {
            checkCancellation()
            val key = service.poll() ?: return
            if (key !== registration) { dirty=true;key.cancel();return }
            var visited=0
            for (event in key.pollEvents()) {
                if(visited++%128==0)checkCancellation()
                if (event.kind() == OVERFLOW) { invalidateForOverflow();key.reset();return }
                val relative = event.context() as? Path
                if (relative == null || relative.isAbsolute || relative.nameCount != 1 || relative.toString() in setOf(".","..")) {
                    dirty=true;continue
                }
                update(relative.toString())
            }
            if (!key.reset()) dirty=true
        }
        // Unbounded external churn cannot trap foreground cancellation here.
        dirty=true
    }
    @Synchronized fun usedBytes(checkCancellation: () -> Unit = {}): Long {
        check(!closed) { "Narration storage ledger is closed" }
        // A bounded retry handles a directory replaced during reconstruction.
        repeat(3) {
            try {
                checkCancellation()
                if (dirty || registration?.isValid != true || identity != identity()) rebuild(checkCancellation)
                if (dirty) return@repeat
                drain(checkCancellation)
                if (!dirty && registration?.isValid == true && identity == identity()) return total
                dirty=true
            } catch (failure: Throwable) {
                dirty=true;discardWatcher();throw failure
            }
        }
        throw IOException("Narration cache kept changing during its storage inventory")
    }
    @Synchronized fun published(file: Path) {
        require(file.parent == directory && audioName(file.fileName.toString())) { "Published audio escaped its storage ledger" }
        check(!closed) { "Narration storage ledger is closed" }
        try {
            // Never start a recovery scan after a WAV has just been published.
            // If the inventory is dirty, its next miss will scan this new file.
            if(dirty || registration?.isValid != true || identity != identity()) { dirty=true;return }
            update(file.fileName.toString())
        } catch(failure: Throwable) { dirty=true;discardWatcher();throw failure }
    }
    @Synchronized fun requireSpace(nextBytes: Long, configured: Long? = null,checkCancellation: () -> Unit = {}): Snapshot {
        val used=usedBytes(checkCancellation);val free=freeBytes().coerceAtLeast(0);checkCancellation()
        val sum=if(Long.MAX_VALUE-used<free)Long.MAX_VALUE else used+free
        val maximum=(sum-RESERVE).coerceAtLeast(0)
        val limit=(configured ?: maximum).coerceAtLeast(0).coerceAtMost(maximum)
        check(free>RESERVE && nextBytes.coerceAtLeast(0)<=limit-used && limit>used) {
            "Narration audio storage limit reached; existing recordings have been preserved"
        }
        return Snapshot(used,free,limit)
    }
    @Synchronized override fun close() {
        closed=true;try { discardWatcher() } finally { sizes.clear();total=0 }
    }
    companion object { const val RESERVE=1_073_741_824L }
}

/** The serialized synthesis lane normally uses one directory. Keep handles and
 * maps bounded if callers alternate isolated cache roots; cache hits create none. */
internal class WindowsAudioStorageLedgers(private val capacity: Int=4) : AutoCloseable {
    private val entries=LinkedHashMap<Path,WindowsAudioStorageLedger>(4,0.75f,true)
    init { require(capacity>0) }
    val observedDirectories: Int @Synchronized get()=entries.size
    private fun path(directory: File)=directory.toPath().toAbsolutePath().normalize()
    @Synchronized fun requireSpace(directory: File,nextBytes: Long,configured: Long?,checkCancellation: () -> Unit = {}) {
        val root=path(directory)
        val ledger=entries[root] ?: run {
            if(entries.size>=capacity){val oldest=entries.entries.first();oldest.value.close();entries.remove(oldest.key)}
            WindowsAudioStorageLedger(root).also { entries[root]=it }
        }
        ledger.requireSpace(nextBytes,configured,checkCancellation)
    }
    @Synchronized fun published(file: File) {
        val parent=path(file.parentFile)
        // Importing a compatible old recording remains a cache hit. Do not
        // build an inventory merely to play/reuse it; a later miss will scan it.
        entries[parent]?.published(file.toPath().toAbsolutePath().normalize())
    }
    @Synchronized override fun close(){
        var failure: Throwable?=null
        for(ledger in entries.values)try { ledger.close() } catch(error: Throwable){
            if(failure==null)failure=error else failure.addSuppressed(error)
        }
        entries.clear();failure?.let { throw it }
    }
}
