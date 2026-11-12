package com.worldrpg.neonlauncher.ui

import android.content.Intent
import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import androidx.fragment.app.Fragment
import androidx.viewpager2.adapter.FragmentStateAdapter
import com.worldrpg.neonlauncher.R
import com.worldrpg.neonlauncher.databinding.ActivityMainBinding
import com.worldrpg.neonlauncher.game.EngineHostActivity
import com.worldrpg.neonlauncher.game.GameActivity
import com.worldrpg.neonlauncher.network.ServerInfo

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        setupViewPager()
        setupBottomNavigation()
    }

    private fun setupViewPager() {
        binding.viewPager.adapter = object : FragmentStateAdapter(supportFragmentManager, lifecycle) {
            override fun getItemCount(): Int = 3

            override fun createFragment(position: Int): Fragment {
                return when (position) {
                    0 -> ServerBrowserFragment()
                    1 -> FavoritesFragment()
                    2 -> SettingsFragment()
                    else -> ServerBrowserFragment()
                }
            }
        }

        // Disable swiping to prevent accidental tab changes during server list scroll
        binding.viewPager.isUserInputEnabled = false
    }

    private fun setupBottomNavigation() {
        binding.bottomNavigation.setOnItemSelectedListener { item ->
            when (item.itemId) {
                R.id.nav_servers -> {
                    binding.viewPager.currentItem = 0
                    true
                }
                R.id.nav_favorites -> {
                    binding.viewPager.currentItem = 1
                    true
                }
                R.id.nav_settings -> {
                    binding.viewPager.currentItem = 2
                    true
                }
                else -> false
            }
        }
    }

    /**
     * Opens the real GTA activity. That host draws the world and the MobileSA
     * touch controls. The blank GameActivity surface only pumped the network.
     */
    fun launchGame(server: ServerInfo) {
        val intent = Intent(this, EngineHostActivity::class.java).apply {
            putExtra(GameActivity.EXTRA_SERVER_IP, server.ip)
            putExtra(GameActivity.EXTRA_SERVER_PORT, server.port)
            putExtra(GameActivity.EXTRA_SERVER_NAME, server.name)
        }
        startActivity(intent)
    }
}