import { SidebarProvider, SidebarTrigger } from "@/components/ui/sidebar"
import { AppSidebar, type Page } from "@/components/AppSidebar"
import { useRoute } from "@/hooks/use-route"
import HomePage from "@/pages/HomePage"
import ConsolePage from "@/pages/ConsolePage"
import SettingsPage from "@/pages/SettingsPage"
import FirmwarePage from "@/pages/FirmwarePage"

function PageContent({ page }: { page: Page }) {
  switch (page) {
    case "home":
      return <HomePage />
    case "console":
      return <ConsolePage />
    case "settings":
      return <SettingsPage />
    case "firmware":
      return <FirmwarePage />
  }
}

export default function App() {
  const { page, navigate } = useRoute()

  return (
    <SidebarProvider>
      <AppSidebar currentPage={page} onNavigate={navigate} />
      <main className="flex h-screen w-full min-w-0 flex-col overflow-hidden p-6">
        <SidebarTrigger className="shrink-0 md:hidden" />
        <div className="min-h-0 w-full flex-1 overflow-y-auto">
          <PageContent page={page} />
        </div>
      </main>
    </SidebarProvider>
  )
}
